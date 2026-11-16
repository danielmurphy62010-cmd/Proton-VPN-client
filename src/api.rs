use anyhow::{Context, Result};
use reqwest::header::{HeaderMap, HeaderValue, ACCEPT, USER_AGENT};
use serde::{Deserialize, Serialize};

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
    pub endpoints: Vec<ServerEndpoint>,
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
    #[serde(rename = "Servers", default)]
    servers: Vec<PhysicalServer>,
}
#[derive(Deserialize)]
struct PhysicalServer {
    #[serde(rename = "ID", default)]
    id: String,
    #[serde(rename = "EntryIP", default)]
    entry_ip: String,
    #[serde(rename = "EntryPerProtocol", default)]
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

pub async fn fetch_logical_servers(
    base_url: &str,
    credentials: &ApiCredentials,
) -> Result<Vec<ProtonServer>> {
    let mut headers = HeaderMap::new();
    headers.insert("x-pm-apiversion", HeaderValue::from_static("3"));
    headers.insert("x-pm-appversion", HeaderValue::from_static("proton-vpn-client@0.2.0"));
    headers.insert("x-pm-locale", HeaderValue::from_static("en-US"));
    headers.insert(ACCEPT, HeaderValue::from_static("application/json"));
    headers.insert(USER_AGENT, HeaderValue::from_static("ProtonVPN-Client/0.2.0"));
    headers.insert("x-pm-uid", HeaderValue::from_str(&credentials.uid)?);
    headers.insert("authorization", HeaderValue::from_str(&format!("Bearer {}", credentials.access_token))?);

    let url = format!("{}/vpn/logicals?SignServer=Server.EntryIP,Server.Label", base_url.trim_end_matches('/'));
    let response = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(25))
        .default_headers(headers)
        .build()?
        .get(url)
        .send().await
        .context("requesting Proton logical server list")?
        .error_for_status()
        .context("Proton API rejected the server-list request; check session token, UID, and account access")?;

    let raw: LogicalResponse = response.json().await.context("decoding Proton LogicalServers JSON")?;
    let mut servers = Vec::with_capacity(raw.logical_servers.len());
    for logical in raw.logical_servers {
        let mut endpoints = Vec::new();
        for physical in logical.servers {
            if let Some(pp) = physical.per_protocol {
                add_endpoint(&mut endpoints, "wireguard_udp", pp.wireguard_udp, &physical.id);
                add_endpoint(&mut endpoints, "wireguard_tcp", pp.wireguard_tcp, &physical.id);
                add_endpoint(&mut endpoints, "wireguard_tls_stealth", pp.wireguard_tls, &physical.id);
                add_endpoint(&mut endpoints, "openvpn_udp", pp.openvpn_udp, &physical.id);
                add_endpoint(&mut endpoints, "openvpn_tcp", pp.openvpn_tcp, &physical.id);
            }
            if !physical.entry_ip.is_empty() && endpoints.is_empty() {
                endpoints.push(ServerEndpoint {
                    protocol: "entry_ip_unmapped".into(), ip: physical.entry_ip,
                    port: None, physical_server: physical.id,
                });
            }
        }
        servers.push(ProtonServer {
            id: logical.id, name: logical.name, country: logical.exit_country,
            city: logical.city, tier: logical.tier, load: logical.load,
            status: logical.status, endpoints,
        });
    }
    Ok(servers)
}

fn add_endpoint(out: &mut Vec<ServerEndpoint>, protocol: &str, entry: Option<ProtocolEntry>, physical_id: &str) {
    if let Some(entry) = entry {
        if !entry.ipv4.is_empty() {
            out.push(ServerEndpoint { protocol: protocol.into(), ip: entry.ipv4, port: None,
                physical_server: physical_id.into() });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn protocol_entries_are_flattened_and_port_is_not_invented() {
        let json = r#"{"LogicalServers":[{"ID":"1","Name":"US#1","ExitCountry":"US","City":"Dallas","Tier":2,"Load":20,"Status":1,"Servers":[{"ID":"p1","EntryIP":"1.2.3.4","EntryPerProtocol":{"WireGuardUDP":{"IPv4":"1.2.3.5"},"OpenVPNTCP":{"IPv4":"1.2.3.6"}}}]}]}"#;
        let parsed: LogicalResponse = serde_json::from_str(json).unwrap();
        assert_eq!(parsed.logical_servers.len(), 1);
        assert_eq!(parsed.logical_servers[0].servers.len(), 1);
    }
}
