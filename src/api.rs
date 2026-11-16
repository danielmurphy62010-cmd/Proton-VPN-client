use anyhow::{Context, Result, bail};

use base64::{engine::general_purpose::STANDARD as BASE64, Engine as _};
use ed25519_dalek::SigningKey;
use rand::rngs::OsRng;
use sha2::{Digest, Sha512};
use x25519_dalek::StaticSecret;

use reqwest::{
    header::{HeaderMap, HeaderValue, ACCEPT, USER_AGENT},
    Client, Proxy,
};
use serde::{de::DeserializeOwned, Deserialize, Serialize};

#[derive(Clone)]
pub struct ApiCredentials {
    pub access_token: String,
    pub uid: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ServerEndpoint {
    pub protocol: String,
    pub ip: String,
    pub port: Option<u16>,
    pub physical_server: String,
    pub domain: Option<String>,
    pub label: Option<String>,
    pub server_public_key: Option<String>,
    pub server_signature: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProtonServer {
    pub id: String,
    pub name: String,
    pub country: String,
    pub city: String,
    pub tier: i8,
    pub load: i8,
    pub status: i8,
    pub features: u64,
    pub endpoints: Vec<ServerEndpoint>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProtonVpnInfo {
    pub plan_name: Option<String>,
    pub plan_title: Option<String>,
    pub status: Option<i32>,
    pub max_tier: Option<i8>,
    pub max_connect: Option<i32>,
    pub is_business: Option<bool>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProtonOpenVpnPorts {
    pub udp: Vec<u16>,
    pub tcp: Vec<u16>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProtonWireGuardPorts {
    pub udp: Vec<u16>,
    pub tcp: Vec<u16>,
    pub tls: Vec<u16>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProtonDefaultPorts {
    pub openvpn: ProtonOpenVpnPorts,
    pub wireguard: ProtonWireGuardPorts,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProtonSmartProtocols {
    pub wireguard_udp: Option<bool>,
    pub wireguard_tcp: Option<bool>,
    pub wireguard_tls: Option<bool>,
    pub openvpn_udp: Option<bool>,
    pub openvpn_tcp: Option<bool>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProtonClientConfig {
    pub default_ports: ProtonDefaultPorts,
    pub smart_protocol: Option<ProtonSmartProtocols>,
    pub server_refresh_interval: Option<i32>,
    pub change_server_attempt_limit: Option<i32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProtonSnapshot {
    pub vpn: ProtonVpnInfo,
    pub client_config: ProtonClientConfig,
    pub servers: Vec<ProtonServer>,
}

#[derive(Deserialize)]
struct LogicalResponse {
    #[serde(rename = "LogicalServers", default)]
    logical_servers: Vec<LogicalServer>,
}

#[derive(Deserialize)]
struct LogicalServer {
    #[serde(rename = "ID", default)]
    id: String,
    #[serde(rename = "Name", default)]
    name: String,
    #[serde(rename = "ExitCountry", default)]
    exit_country: String,
    #[serde(rename = "City", default)]
    city: String,
    #[serde(rename = "Tier", default)]
    tier: i8,
    #[serde(rename = "Load", default)]
    load: i8,
    #[serde(rename = "Status", default)]
    status: i8,
    #[serde(rename = "Features", default)]
    features: u64,
    #[serde(rename = "Servers", default)]
    servers: Vec<PhysicalServer>,
}

#[derive(Deserialize)]
struct PhysicalServer {
    #[serde(rename = "ID", default)]
    id: String,
    #[serde(rename = "EntryIP", default)]
    entry_ip: String,
    #[serde(rename = "Domain")]
    domain: Option<String>,
    #[serde(rename = "Label")]
    label: Option<String>,
    #[serde(rename = "X25519PublicKey")]
    x25519_public_key: Option<String>,
    #[serde(rename = "Signature")]
    signature: Option<String>,
    #[serde(rename = "EntryPerProtocol")]
    per_protocol: Option<ProtocolEntries>,
}

#[derive(Deserialize, Default)]
struct ProtocolEntries {
    #[serde(rename = "WireGuardUDP")]
    wireguard_udp: Option<ProtocolEntry>,
    #[serde(rename = "WireGuardTCP")]
    wireguard_tcp: Option<ProtocolEntry>,
    #[serde(rename = "WireGuardTLS")]
    wireguard_tls: Option<ProtocolEntry>,
    #[serde(rename = "OpenVPNUDP")]
    openvpn_udp: Option<ProtocolEntry>,
    #[serde(rename = "OpenVPNTCP")]
    openvpn_tcp: Option<ProtocolEntry>,
}

#[derive(Deserialize)]
struct ProtocolEntry {
    #[serde(rename = "IPv4", default)]
    ipv4: String,
}

#[derive(Deserialize, Default)]
struct VpnInfoWrapper {
    #[serde(rename = "Vpn")]
    vpn: Option<VpnInfo>,
}

#[derive(Deserialize, Default)]
struct VpnInfo {
    #[serde(rename = "PlanName")]
    plan_name: Option<String>,
    #[serde(rename = "PlanTitle")]
    plan_title: Option<String>,
    #[serde(rename = "Status")]
    status: Option<i32>,
    #[serde(rename = "MaxTier")]
    max_tier: Option<i8>,
    #[serde(rename = "MaxConnect")]
    max_connect: Option<i32>,
    #[serde(rename = "IsBusiness")]
    is_business: Option<bool>,
}

#[derive(Deserialize, Default)]
struct ClientConfigResponse {
    #[serde(rename = "DefaultPorts", default)]
    default_ports: DefaultPortsResponse,
    #[serde(rename = "SmartProtocol")]
    smart_protocol: Option<SmartProtocolResponse>,
    #[serde(rename = "ServerRefreshInterval")]
    server_refresh_interval: Option<i32>,
    #[serde(rename = "ChangeServerAttemptLimit")]
    change_server_attempt_limit: Option<i32>,
}

#[derive(Deserialize, Default)]
struct DefaultPortsResponse {
    #[serde(rename = "OpenVPN", default)]
    openvpn: OpenVpnPortsResponse,
    #[serde(rename = "WireGuard", default)]
    wireguard: WireGuardPortsResponse,
}

#[derive(Deserialize, Default)]
struct OpenVpnPortsResponse {
    #[serde(rename = "UDP", default)]
    udp: Vec<u16>,
    #[serde(rename = "TCP", default)]
    tcp: Vec<u16>,
}

#[derive(Deserialize, Default)]
struct WireGuardPortsResponse {
    #[serde(rename = "UDP", default)]
    udp: Vec<u16>,
    #[serde(rename = "TCP", default)]
    tcp: Vec<u16>,
    #[serde(rename = "TLS", default)]
    tls: Vec<u16>,
}

#[derive(Deserialize, Default)]
struct SmartProtocolResponse {
    #[serde(rename = "WireGuard")]
    wireguard_udp: Option<bool>,
    #[serde(rename = "WireGuardTCP")]
    wireguard_tcp: Option<bool>,
    #[serde(rename = "WireGuardTLS")]
    wireguard_tls: Option<bool>,
    #[serde(rename = "OpenVPN")]
    openvpn_udp: Option<bool>,
    #[serde(rename = "OpenVPNTCP")]
    openvpn_tcp: Option<bool>,
}


#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProtonCertificateStatus {
    pub expiration_time: i64,
    pub refresh_time: i64,
    pub server_public_key: Option<String>,
}

#[derive(Serialize)]
#[serde(rename_all = "PascalCase")]
struct CertificateRequest {
    client_public_key: String,
    client_public_key_mode: &'static str,
    device_name: &'static str,
    duration: Option<String>,
    mode: &'static str,
    features: Vec<String>,
}

#[derive(Deserialize)]
struct CertificateResponse {
    #[serde(rename = "Certificate", default)]
    certificate: String,
    #[serde(rename = "ExpirationTime", default)]
    expiration_time: i64,
    #[serde(rename = "RefreshTime", default)]
    refresh_time: i64,
    #[serde(rename = "ServerPublicKey")]
    server_public_key: Option<String>,
}

// This key material intentionally isn't Serialize. It lives only for this process lifetime.
pub struct ProtonVpnCredentials {
    _ed25519_signing_key: SigningKey,
    wireguard_private_key: StaticSecret,
    pub client_public_key_pem: String,
    pub certificate_pem: String,
    pub status: ProtonCertificateStatus,
}

impl ProtonVpnCredentials {
    pub fn wireguard_private_key_base64(&self) -> String {
        BASE64.encode(self.wireguard_private_key.to_bytes())
    }

    pub fn wireguard_private_key_bytes(&self) -> [u8; 32] {
        self.wireguard_private_key.to_bytes()
    }
}

fn ed25519_public_key_pem(signing_key: &SigningKey) -> String {
    // RFC 8410 SubjectPublicKeyInfo for Ed25519:
    // SEQUENCE(AlgorithmIdentifier(Ed25519), BIT STRING(public key)).
    const SPKI_PREFIX: [u8; 12] = [
        0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00,
    ];
    let mut der = Vec::with_capacity(SPKI_PREFIX.len() + 32);
    der.extend_from_slice(&SPKI_PREFIX);
    der.extend_from_slice(&signing_key.verifying_key().to_bytes());
    let encoded = BASE64.encode(der);
    let lines = encoded.as_bytes().chunks(64)
        .map(|line| std::str::from_utf8(line).expect("base64 output is ASCII"))
        .collect::<Vec<_>>()
        .join("\n");
    format!("-----BEGIN PUBLIC KEY-----\n{lines}\n-----END PUBLIC KEY-----")
}

fn ed25519_seed_to_x25519_secret(seed: &[u8; 32]) -> StaticSecret {
    // RFC 7748 conversion used by Proton's Ed25519 -> X25519 key derivation:
    // SHA-512(seed), take first 32 octets, then clamp as an X25519 scalar.
    let digest = Sha512::digest(seed);
    let mut scalar = [0u8; 32];
    scalar.copy_from_slice(&digest[..32]);
    scalar[0] &= 248;
    scalar[31] &= 127;
    scalar[31] |= 64;
    StaticSecret::from(scalar)
}

pub async fn request_connection_credentials(
    base_url: &str,
    credentials: &ApiCredentials,
) -> Result<ProtonVpnCredentials> {
    let signing_key = SigningKey::generate(&mut OsRng);
    let client_public_key_pem = ed25519_public_key_pem(&signing_key);
    let wireguard_private_key = ed25519_seed_to_x25519_secret(&signing_key.to_bytes());

    let request_body = CertificateRequest {
        client_public_key: client_public_key_pem.clone(),
        client_public_key_mode: "EC",
        device_name: "",
        duration: None,
        mode: "session",
        features: Vec::new(),
    };

    let client = proton_client(base_url, credentials)?;
    let url = format!("{}/vpn/v1/certificate", base_url.trim_end_matches('/'));
    let response: CertificateResponse = client
        .post(url)
        .json(&request_body)
        .send()
        .await
        .context("requesting Proton device certificate through Tor")?
        .error_for_status()
        .context("Proton rejected the device certificate request")?
        .json()
        .await
        .context("decoding Proton device certificate response")?;

    if response.certificate.trim().is_empty() {
        bail!("Proton returned an empty device certificate");
    }
    if response.expiration_time <= chrono::Utc::now().timestamp() {
        bail!("Proton returned an expired device certificate");
    }

    let status = ProtonCertificateStatus {
        expiration_time: response.expiration_time,
        refresh_time: response.refresh_time,
        server_public_key: response.server_public_key,
    };

    Ok(ProtonVpnCredentials {
        _ed25519_signing_key: signing_key,
        wireguard_private_key,
        client_public_key_pem,
        certificate_pem: response.certificate,
        status,
    })
}

fn proton_client(_base_url: &str, credentials: &ApiCredentials) -> Result<Client> {
    let mut headers = HeaderMap::new();
    headers.insert("x-pm-apiversion", HeaderValue::from_static("3"));
    headers.insert("x-pm-appversion", HeaderValue::from_static("proton-vpn-client@0.3.0"));
    headers.insert("x-pm-locale", HeaderValue::from_static("en-US"));
    headers.insert(ACCEPT, HeaderValue::from_static("application/json"));
    headers.insert(USER_AGENT, HeaderValue::from_static("ProtonVPN-Client/0.3.0"));
    headers.insert("x-pm-uid", HeaderValue::from_str(&credentials.uid)?);
    headers.insert(
        "authorization",
        HeaderValue::from_str(&format!("Bearer {}", credentials.access_token))?,
    );

    // Disable environment/system proxies, then force every Proton request through Tor.
    // socks5h keeps DNS resolution on the Tor side rather than leaking DNS locally.
    let tor_proxy = Proxy::all("socks5h://127.0.0.1:9050")
        .context("creating Tor SOCKS5 proxy")?;
    Ok(Client::builder()
        .no_proxy()
        .proxy(tor_proxy)
        .default_headers(headers)
        .timeout(std::time::Duration::from_secs(30))
        .build()?)
}

async fn get_json<T: DeserializeOwned>(client: &Client, url: String, label: &str) -> Result<T> {
    let response = client
        .get(&url)
        .send()
        .await
        .with_context(|| format!("requesting Proton {label} through Tor"))?
        .error_for_status()
        .with_context(|| format!("Proton rejected the {label} request; check the session and VPN entitlement"))?;
    response
        .json::<T>()
        .await
        .with_context(|| format!("decoding Proton {label} response"))
}

pub async fn fetch_snapshot(
    base_url: &str,
    credentials: &ApiCredentials,
) -> Result<ProtonSnapshot> {
    let client = proton_client(base_url, credentials)?;
    let base = base_url.trim_end_matches('/');

    // Match the current official Windows API request: ask for state and all
    // published protocol entries instead of inferring ports or endpoints.
    let logicals_url = format!(
        "{base}/vpn/v2/logicals?SignServer=Server.EntryIP,Server.Label&SecureCoreFilter=all&WithState=true&WithEntriesForProtocols=WireGuardUDP,WireGuardTCP,WireGuardTLS,OpenVPNUDP,OpenVPNTCP"
    );

    let (logicals, vpn_wrapper, client_config): (
        LogicalResponse,
        VpnInfoWrapper,
        ClientConfigResponse,
    ) = tokio::try_join!(
        get_json(&client, logicals_url, "logical server list"),
        get_json(&client, format!("{base}/vpn/v2"), "VPN account information"),
        get_json(&client, format!("{base}/vpn/v2/clientconfig"), "VPN client configuration"),
    )?;

    let vpn = vpn_wrapper.vpn.map(|value| ProtonVpnInfo {
        plan_name: value.plan_name,
        plan_title: value.plan_title,
        status: value.status,
        max_tier: value.max_tier,
        max_connect: value.max_connect,
        is_business: value.is_business,
    }).unwrap_or_default();

    let client_config = ProtonClientConfig {
        default_ports: ProtonDefaultPorts {
            openvpn: ProtonOpenVpnPorts {
                udp: client_config.default_ports.openvpn.udp,
                tcp: client_config.default_ports.openvpn.tcp,
            },
            wireguard: ProtonWireGuardPorts {
                udp: client_config.default_ports.wireguard.udp,
                tcp: client_config.default_ports.wireguard.tcp,
                tls: client_config.default_ports.wireguard.tls,
            },
        },
        smart_protocol: client_config.smart_protocol.map(|value| ProtonSmartProtocols {
            wireguard_udp: value.wireguard_udp,
            wireguard_tcp: value.wireguard_tcp,
            wireguard_tls: value.wireguard_tls,
            openvpn_udp: value.openvpn_udp,
            openvpn_tcp: value.openvpn_tcp,
        }),
        server_refresh_interval: client_config.server_refresh_interval,
        change_server_attempt_limit: client_config.change_server_attempt_limit,
    };

    let mut servers = Vec::with_capacity(logicals.logical_servers.len());
    for logical in logicals.logical_servers {
        let mut endpoints = Vec::new();
        for physical in logical.servers {
            if let Some(entries) = physical.per_protocol.as_ref() {
                add_endpoints(
                    &mut endpoints, "wireguard_udp", entries.wireguard_udp.as_ref(),
                    &client_config.default_ports.wireguard.udp, &physical,
                );
                add_endpoints(
                    &mut endpoints, "wireguard_tcp", entries.wireguard_tcp.as_ref(),
                    &client_config.default_ports.wireguard.tcp, &physical,
                );
                add_endpoints(
                    &mut endpoints, "wireguard_tls", entries.wireguard_tls.as_ref(),
                    &client_config.default_ports.wireguard.tls, &physical,
                );
                add_endpoints(
                    &mut endpoints, "openvpn_udp", entries.openvpn_udp.as_ref(),
                    &client_config.default_ports.openvpn.udp, &physical,
                );
                add_endpoints(
                    &mut endpoints, "openvpn_tcp", entries.openvpn_tcp.as_ref(),
                    &client_config.default_ports.openvpn.tcp, &physical,
                );
            }

            // Preserve the physical entry metadata for diagnostics if the API
            // does not return protocol-specific entries for this host.
            if !physical.entry_ip.is_empty()
                && !endpoints.iter().any(|entry| entry.physical_server == physical.id)
            {
                endpoints.push(ServerEndpoint {
                    protocol: "entry_ip_unmapped".into(),
                    ip: physical.entry_ip,
                    port: None,
                    physical_server: physical.id,
                    domain: physical.domain,
                    label: physical.label,
                    server_public_key: physical.x25519_public_key,
                    server_signature: physical.signature,
                });
            }
        }

        servers.push(ProtonServer {
            id: logical.id,
            name: logical.name,
            country: logical.exit_country,
            city: logical.city,
            tier: logical.tier,
            load: logical.load,
            status: logical.status,
            features: logical.features,
            endpoints,
        });
    }

    Ok(ProtonSnapshot { vpn, client_config, servers })
}

fn add_endpoints(
    out: &mut Vec<ServerEndpoint>,
    protocol: &str,
    entry: Option<&ProtocolEntry>,
    ports: &[u16],
    physical: &PhysicalServer,
) {
    let Some(entry) = entry else { return; };
    if entry.ipv4.is_empty() { return; }

    // Retain every server-advertised port for retry/fallback selection.
    if ports.is_empty() {
        out.push(make_endpoint(protocol, &entry.ipv4, None, physical));
    } else {
        for port in ports {
            out.push(make_endpoint(protocol, &entry.ipv4, Some(*port), physical));
        }
    }
}

fn make_endpoint(
    protocol: &str,
    ip: &str,
    port: Option<u16>,
    physical: &PhysicalServer,
) -> ServerEndpoint {
    ServerEndpoint {
        protocol: protocol.into(),
        ip: ip.into(),
        port,
        physical_server: physical.id.clone(),
        domain: physical.domain.clone(),
        label: physical.label.clone(),
        server_public_key: physical.x25519_public_key.clone(),
        server_signature: physical.signature.clone(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn api_server_entries_are_expanded_using_server_advertised_ports() {
        let json = r#"{
            "LogicalServers":[{
                "ID":"logical-1","Name":"US#1","ExitCountry":"US","City":"Dallas",
                "Tier":2,"Load":20,"Status":1,"Features":9,
                "Servers":[{
                    "ID":"physical-1","EntryIP":"203.0.113.7","Domain":"node.example",
                    "Label":"server-label","X25519PublicKey":"server-key","Signature":"signed-entry",
                    "EntryPerProtocol":{
                        "WireGuardUDP":{"IPv4":"203.0.113.8"},
                        "WireGuardTLS":{"IPv4":"203.0.113.9"},
                        "OpenVPNTCP":{"IPv4":"203.0.113.10"}
                    }
                }]
            }]
        }"#;
        let parsed: LogicalResponse = serde_json::from_str(json).unwrap();
        let physical = &parsed.logical_servers[0].servers[0];
        let entries = physical.per_protocol.as_ref().unwrap();
        let mut out = Vec::new();
        add_endpoints(
            &mut out, "wireguard_udp",
            entries.wireguard_udp.as_ref(),
            &[443, 1194], physical,
        );
        assert_eq!(out.len(), 2);
        assert_eq!(out[0].port, Some(443));
        assert_eq!(out[1].port, Some(1194));
        assert_eq!(out[0].server_public_key.as_deref(), Some("server-key"));
        assert_eq!(parsed.logical_servers[0].features, 9);
    }

    #[test]
    fn vpn_client_config_deserializes_protocol_port_lists() {
        let json = r#"{
            "DefaultPorts":{
                "OpenVPN":{"UDP":[80,443],"TCP":[443]},
                "WireGuard":{"UDP":[88,51820],"TCP":[443],"TLS":[443]}
            },
            "SmartProtocol":{"WireGuard":true,"WireGuardTCP":true,"WireGuardTLS":false,"OpenVPN":true,"OpenVPNTCP":true},
            "ServerRefreshInterval":120
        }"#;
        let parsed: ClientConfigResponse = serde_json::from_str(json).unwrap();
        assert_eq!(parsed.default_ports.wireguard.udp, vec![88, 51820]);
        assert_eq!(parsed.default_ports.openvpn.tcp, vec![443]);
        assert_eq!(parsed.smart_protocol.unwrap().wireguard_udp, Some(true));
    }
}
