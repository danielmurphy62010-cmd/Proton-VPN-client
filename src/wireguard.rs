//! In-process WireGuard UDP tunnel for Windows.
//! BoringTun handles the WireGuard protocol; Wintun provides the L3 adapter.

use anyhow::{bail, Context, Result};
use boringtun::{
    noise::{Tunn, TunnResult},
    x25519::{PublicKey, StaticSecret},
};
use std::{
    net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr},
    process::Command,
    sync::{Arc, Mutex},
    time::Duration,
};
use tokio::{
    net::UdpSocket,
    sync::{mpsc, watch},
    task::JoinHandle,
    time,
};
use tracing::{debug, warn};
use wintun::{Adapter, Session, Wintun, MAX_RING_CAPACITY};

const ADAPTER_NAME: &str = "ProtonVPN";
const CLIENT_IPV4: Ipv4Addr = Ipv4Addr::new(10, 2, 0, 2);
const GATEWAY_IPV4: Ipv4Addr = Ipv4Addr::new(10, 2, 0, 1);
const IPV4_MASK: Ipv4Addr = Ipv4Addr::new(255, 255, 255, 255);
const CLIENT_IPV6: Ipv6Addr = Ipv6Addr::new(0x2a07, 0xb944, 0, 0, 0, 2, 0, 2);
const GATEWAY_IPV6: Ipv6Addr = Ipv6Addr::new(0x2a07, 0xb944, 0, 0, 0, 2, 0, 1);
const IPV6_MASK: Ipv6Addr = Ipv6Addr::new(u16::MAX, u16::MAX, u16::MAX, u16::MAX, u16::MAX, u16::MAX, u16::MAX, u16::MAX);

enum OwnedResult {
    Done,
    Network(Vec<u8>),
    Tunnel(Vec<u8>),
    Error(String),
}

fn own_result(result: TunnResult<'_>) -> OwnedResult {
    match result {
        TunnResult::Done => OwnedResult::Done,
        TunnResult::Err(error) => OwnedResult::Error(format!("{error:?}")),
        TunnResult::WriteToNetwork(packet) => OwnedResult::Network(packet.to_vec()),
        TunnResult::WriteToTunnelV4(packet, _) => OwnedResult::Tunnel(packet.to_vec()),
        TunnResult::WriteToTunnelV6(packet, _) => OwnedResult::Tunnel(packet.to_vec()),
    }
}

fn route_command(args: &[String]) -> Result<()> {
    let output = Command::new("route.exe")
        .args(args)
        .output()
        .context("starting Windows route.exe")?;
    if !output.status.success() {
        bail!(
            "route.exe {} failed: {}",
            args.join(" "),
            String::from_utf8_lossy(&output.stderr).trim()
        );
    }
    Ok(())
}

fn netsh_command(args: &[String]) -> Result<()> {
    let output = Command::new("netsh.exe")
        .args(args)
        .output()
        .context("starting Windows netsh.exe")?;
    if !output.status.success() {
        bail!(
            "netsh.exe {} failed: {}",
            args.join(" "),
            String::from_utf8_lossy(&output.stderr).trim()
        );
    }
    Ok(())
}

struct RouteCleanup {
    endpoint: SocketAddr,
    physical_gateway: Ipv4Addr,
    adapter_index: u32,
    ipv4_added: bool,
    ipv6_added: bool,
    host_route_added: bool,
}

impl RouteCleanup {
    fn new(endpoint: SocketAddr, physical_gateway: Ipv4Addr, adapter_index: u32) -> Self {
        Self {
            endpoint,
            physical_gateway,
            adapter_index,
            ipv4_added: false,
            ipv6_added: false,
            host_route_added: false,
        }
    }

    fn add_physical_endpoint_route(&mut self) -> Result<()> {
        let IpAddr::V4(endpoint_ip) = self.endpoint.ip() else {
            bail!("The native WireGuard UDP adapter currently requires an IPv4 Proton relay address");
        };
        route_command(&[
            "ADD".into(),
            endpoint_ip.to_string(),
            "MASK".into(),
            "255.255.255.255".into(),
            self.physical_gateway.to_string(),
            "METRIC".into(),
            "1".into(),
        ])?;
        self.host_route_added = true;
        Ok(())
    }

    fn add_default_routes(&mut self) -> Result<()> {
        route_command(&[
            "ADD".into(),
            "0.0.0.0".into(),
            "MASK".into(),
            "0.0.0.0".into(),
            GATEWAY_IPV4.to_string(),
            "METRIC".into(),
            "5".into(),
            "IF".into(),
            self.adapter_index.to_string(),
        ])?;
        self.ipv4_added = true;

        netsh_command(&[
            "interface".into(),
            "ipv6".into(),
            "add".into(),
            "route".into(),
            "prefix=::/0".into(),
            format!("interface={}", self.adapter_index),
            format!("nexthop={GATEWAY_IPV6}"),
            "metric=5".into(),
            "store=active".into(),
        ])?;
        self.ipv6_added = true;
        Ok(())
    }
}

impl Drop for RouteCleanup {
    fn drop(&mut self) {
        if self.ipv6_added {
            let _ = netsh_command(&[
                "interface".into(),
                "ipv6".into(),
                "delete".into(),
                "route".into(),
                "prefix=::/0".into(),
                format!("interface={}", self.adapter_index),
                format!("nexthop={GATEWAY_IPV6}"),
                "store=active".into(),
            ]);
        }
        if self.ipv4_added {
            let _ = route_command(&[
                "DELETE".into(),
                "0.0.0.0".into(),
                "MASK".into(),
                "0.0.0.0".into(),
                GATEWAY_IPV4.to_string(),
                "IF".into(),
                self.adapter_index.to_string(),
            ]);
        }
        if self.host_route_added {
            if let IpAddr::V4(endpoint_ip) = self.endpoint.ip() {
                let _ = route_command(&[
                    "DELETE".into(),
                    endpoint_ip.to_string(),
                    "MASK".into(),
                    "255.255.255.255".into(),
                    self.physical_gateway.to_string(),
                ]);
            }
        }
    }
}

pub struct WireGuardTunnel {
    shutdown: watch::Sender<bool>,
    tasks: Vec<JoinHandle<()>>,
    session: Option<Arc<Session>>,
    adapter: Option<Arc<Adapter>>,
    _wintun: Wintun,
    _routes: RouteCleanup,
}

impl WireGuardTunnel {
    pub async fn start(
        endpoint: SocketAddr,
        client_private_key: [u8; 32],
        server_public_key: [u8; 32],
    ) -> Result<Self> {
        if !endpoint.is_ipv4() {
            bail!("The first native Proton WireGuard adapter supports IPv4 relay endpoints only");
        }

        let physical_gateway = wintun::get_active_network_interface_gateways()?
            .into_iter()
            .find_map(|ip| match ip {
                IpAddr::V4(gateway) => Some(gateway),
                IpAddr::V6(_) => None,
            })
            .context("no active IPv4 gateway is available for the endpoint bypass route")?;

        let app_dir = std::env::current_exe()?
            .parent()
            .context("application executable has no parent directory")?
            .to_path_buf();
        let wintun_path = app_dir.join("wintun.dll");
        if !wintun_path.is_file() {
            bail!(
                "Wintun runtime is missing at {}. Use the complete portable package.",
                wintun_path.display()
            );
        }
        // SAFETY: CI packages the unmodified, SHA-256-verified official Wintun DLL.
        let wintun = unsafe { wintun::load_from_path(&wintun_path) }
            .context("loading the bundled Wintun runtime")?;

        let adapter = match Adapter::open(&wintun, ADAPTER_NAME) {
            Ok(adapter) => adapter,
            Err(_) => Adapter::create(&wintun, ADAPTER_NAME, "Wintun", None)
                .context("creating the Proton Wintun adapter; run the client elevated")?,
        };

        adapter.set_mtu(1420).context("setting WireGuard adapter MTU")?;
        adapter
            .set_network_addresses_tuple(
                IpAddr::V4(CLIENT_IPV4),
                IpAddr::V4(IPV4_MASK),
                Some(IpAddr::V4(GATEWAY_IPV4)),
            )
            .context("configuring IPv4 on the WireGuard adapter")?;
        adapter
            .set_network_addresses_tuple(
                IpAddr::V6(CLIENT_IPV6),
                IpAddr::V6(IPV6_MASK),
                Some(IpAddr::V6(GATEWAY_IPV6)),
            )
            .context("configuring IPv6 on the WireGuard adapter")?;
        adapter
            .set_dns_servers(&[IpAddr::V4(GATEWAY_IPV4), IpAddr::V6(GATEWAY_IPV6)])
            .context("configuring Proton DNS on the WireGuard adapter")?;

        let adapter_index = adapter.get_adapter_index()?;
        let mut routes = RouteCleanup::new(endpoint, physical_gateway, adapter_index);
        routes.add_physical_endpoint_route()?;
        routes.add_default_routes()?;

        let session = Arc::new(adapter.start_session(MAX_RING_CAPACITY)?);
        let socket = Arc::new(
            UdpSocket::bind("0.0.0.0:0")
                .await
                .context("binding WireGuard UDP transport")?,
        );

        let tunnel = Arc::new(Mutex::new(Tunn::new(
            StaticSecret::from(client_private_key),
            PublicKey::from(server_public_key),
            None,
            Some(25),
            0,
            None,
        )));

        let (shutdown, _) = watch::channel(false);
        let (tun_tx, mut tun_rx) = mpsc::channel::<Vec<u8>>(256);
        let mut tasks = Vec::new();

        // Wintun reads synchronously; isolate the blocking reader from Tokio's executor.
        {
            let reader_session = session.clone();
            let cancel = shutdown.subscribe();
            tasks.push(tokio::task::spawn_blocking(move || {
                loop {
                    if *cancel.borrow() {
                        break;
                    }
                    match reader_session.receive_blocking() {
                        Ok(packet) => {
                            if tun_tx.blocking_send(packet.bytes().to_vec()).is_err() {
                                break;
                            }
                        }
                        Err(_) => break,
                    }
                }
            }));
        }

        // TUN -> WireGuard -> UDP.
        {
            let socket = socket.clone();
            let tunnel = tunnel.clone();
            let mut cancel = shutdown.subscribe();
            tasks.push(tokio::spawn(async move {
                loop {
                    tokio::select! {
                        changed = cancel.changed() => {
                            if changed.is_err() || *cancel.borrow() { break; }
                        }
                        packet = tun_rx.recv() => {
                            let Some(packet) = packet else { break; };
                            let output = {
                                let mut scratch = vec![0u8; packet.len() + 512];
                                let mut state = tunnel.lock().expect("WireGuard tunnel mutex poisoned");
                                own_result(state.encapsulate(&packet, &mut scratch))
                            };
                            if let OwnedResult::Network(datagram) = output {
                                if let Err(error) = socket.send_to(&datagram, endpoint).await {
                                    warn!(%error, "WireGuard UDP send failed");
                                }
                            } else if let OwnedResult::Error(error) = output {
                                debug!(%error, "WireGuard rejected a TUN packet");
                            }
                        }
                    }
                }
            }));
        }

        // UDP -> WireGuard -> TUN. Drain all queued WireGuard network outputs.
        {
            let socket = socket.clone();
            let tunnel = tunnel.clone();
            let session = session.clone();
            let mut cancel = shutdown.subscribe();
            tasks.push(tokio::spawn(async move {
                let mut datagram = vec![0u8; 65535];
                loop {
                    let received = tokio::select! {
                        changed = cancel.changed() => {
                            if changed.is_err() || *cancel.borrow() { break; }
                            continue;
                        }
                        value = socket.recv_from(&mut datagram) => value,
                    };
                    let (count, source) = match received {
                        Ok(value) => value,
                        Err(error) => {
                            warn!(%error, "WireGuard UDP receive failed");
                            break;
                        }
                    };
                    if source != endpoint {
                        continue;
                    }

                    let outputs = {
                        let mut scratch = vec![0u8; 65535];
                        let mut state = tunnel.lock().expect("WireGuard tunnel mutex poisoned");
                        let mut result = own_result(
                            state.decapsulate(Some(source.ip()), &datagram[..count], &mut scratch)
                        );
                        let mut outputs = Vec::new();
                        loop {
                            match result {
                                OwnedResult::Done => break,
                                OwnedResult::Error(error) => {
                                    debug!(%error, "WireGuard dropped an unauthenticated datagram");
                                    break;
                                }
                                OwnedResult::Network(packet) => {
                                    outputs.push((true, packet));
                                    result = own_result(state.decapsulate(None, &[], &mut scratch));
                                }
                                OwnedResult::Tunnel(packet) => {
                                    outputs.push((false, packet));
                                    result = own_result(state.decapsulate(None, &[], &mut scratch));
                                }
                            }
                        }
                        outputs
                    };

                    for (network, packet) in outputs {
                        if network {
                            if let Err(error) = socket.send_to(&packet, endpoint).await {
                                warn!(%error, "WireGuard response send failed");
                            }
                        } else if let Err(error) = write_tun_packet(&session, &packet) {
                            debug!(%error, "Could not inject decrypted packet into Wintun");
                        }
                    }
                }
            }));
        }

        // WireGuard handshake retry, rekey and persistent keepalive timers.
        {
            let socket = socket.clone();
            let tunnel = tunnel.clone();
            let mut cancel = shutdown.subscribe();
            tasks.push(tokio::spawn(async move {
                let mut interval = time::interval(Duration::from_secs(1));
                let mut scratch = vec![0u8; 256];
                loop {
                    tokio::select! {
                        changed = cancel.changed() => {
                            if changed.is_err() || *cancel.borrow() { break; }
                        }
                        _ = interval.tick() => {
                            let output = {
                                let mut state = tunnel.lock().expect("WireGuard tunnel mutex poisoned");
                                own_result(state.update_timers(&mut scratch))
                            };
                            if let OwnedResult::Network(packet) = output {
                                if let Err(error) = socket.send_to(&packet, endpoint).await {
                                    warn!(%error, "WireGuard timer packet send failed");
                                }
                            }
                        }
                    }
                }
            }));
        }

        // Initiate the authenticated Noise handshake immediately.
        let first_handshake = {
            let mut scratch = vec![0u8; 148];
            let mut state = tunnel.lock().expect("WireGuard tunnel mutex poisoned");
            own_result(state.format_handshake_initiation(&mut scratch, false))
        };
        match first_handshake {
            OwnedResult::Network(packet) => {
                socket.send_to(&packet, endpoint).await
                    .context("sending WireGuard handshake initiation")?;
            }
            OwnedResult::Error(error) => bail!("WireGuard could not initiate handshake: {error}"),
            _ => bail!("WireGuard did not generate a handshake initiation packet"),
        }

        // Do not claim a connection until the peer completes authentication.
        let deadline = time::Instant::now() + Duration::from_secs(25);
        loop {
            if tunnel.lock().expect("WireGuard tunnel mutex poisoned")
                .time_since_last_handshake().is_some()
            {
                break;
            }
            if time::Instant::now() >= deadline {
                let _ = session.shutdown();
                let _ = shutdown.send(true);
                bail!("Proton WireGuard handshake timed out; verify the certificate, peer key and endpoint");
            }
            time::sleep(Duration::from_millis(250)).await;
        }

        Ok(Self {
            shutdown,
            tasks,
            session: Some(session),
            adapter: Some(adapter),
            _wintun: wintun,
            _routes: routes,
        })
    }

    pub async fn stop(mut self) {
        let _ = self.shutdown.send(true);
        if let Some(session) = self.session.as_ref() {
            let _ = session.shutdown();
        }
        for task in self.tasks.drain(..) {
            let _ = time::timeout(Duration::from_secs(2), task).await;
        }
        self.session.take();
        if let Some(adapter) = self.adapter.take() {
            if let Ok(adapter) = Arc::try_unwrap(adapter) {
                let _ = adapter.delete();
            }
        }
    }
}

fn write_tun_packet(session: &Arc<Session>, bytes: &[u8]) -> Result<()> {
    if bytes.is_empty() || bytes.len() > u16::MAX as usize {
        bail!("WireGuard returned an invalid inner packet length: {}", bytes.len());
    }
    let mut packet = session.allocate_send_packet(bytes.len() as u16)?;
    packet.bytes_mut().copy_from_slice(bytes);
    session.send_packet(packet);
    Ok(())
}
