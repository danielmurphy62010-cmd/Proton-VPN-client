use crate::runtime::{AppState, ProxyRoute};
use anyhow::{bail, Context, Result};
use tokio::{
    io::{AsyncReadExt, AsyncWriteExt},
    net::TcpStream,
};
use tokio_socks::tcp::Socks5Stream;

pub async fn handle_client(mut client: TcpStream, app: AppState) -> Result<()> {
    let mut buf = vec![0u8; 16 * 1024];
    let n = client.read(&mut buf).await.context("reading proxy request")?;
    if n == 0 { return Ok(()); }
    buf.truncate(n);
    let header_end = find_header_end(&buf).unwrap_or(buf.len());
    let header = String::from_utf8_lossy(&buf[..header_end]);
    let first_line = header.lines().next().context("empty HTTP request")?;
    let (target, rewritten) = parse_request(first_line, &header)?;
    let route = app.route_for_proxy().await;
    let mut upstream = connect_target(&target, route).await?;
    if let Some(rewritten) = rewritten {
        upstream.write_all(rewritten.as_bytes()).await?;
    } else {
        client.write_all(b"HTTP/1.1 200 Connection Established\r\nProxy-Agent: Proton-VPN-Client\r\n\r\n").await?;
    }
    if header_end < buf.len() {
        upstream.write_all(&buf[header_end..]).await?;
    }
    tokio::io::copy_bidirectional(&mut client, &mut upstream).await?;
    Ok(())
}

fn find_header_end(bytes: &[u8]) -> Option<usize> {
    bytes.windows(4).position(|w| w == b"\r\n\r\n").map(|p| p + 4)
}

fn parse_request(first_line: &str, header: &str) -> Result<(String, Option<String>)> {
    let mut parts = first_line.split_whitespace();
    let method = parts.next().context("missing HTTP method")?;
    let uri = parts.next().context("missing HTTP URI")?;
    let version = parts.next().unwrap_or("HTTP/1.1");
    if method.eq_ignore_ascii_case("CONNECT") {
        return Ok((uri.to_string(), None));
    }
    if let Some(rest) = uri.strip_prefix("http://") {
        let (authority, path) = match rest.find('/') {
            Some(index) => (&rest[..index], &rest[index..]),
            None => (rest, "/"),
        };
        let target = if authority.contains(':') { authority.to_string() } else { format!("{authority}:80") };
        let mut rewritten = format!("{method} {path} {version}\r\n");
        let mut host_present = false;
        for line in header.lines().skip(1) {
            if line.is_empty() { continue; }
            if line.to_ascii_lowercase().starts_with("proxy-connection:") { continue; }
            if line.to_ascii_lowercase().starts_with("host:") { host_present = true; }
            rewritten.push_str(line);
            rewritten.push_str("\r\n");
        }
        if !host_present { rewritten.push_str(&format!("Host: {authority}\r\n")); }
        rewritten.push_str("Connection: close\r\n\r\n");
        return Ok((target, Some(rewritten)));
    }
    bail!("only HTTP absolute-form requests and HTTPS CONNECT requests are supported")
}

async fn connect_target(target: &str, route: ProxyRoute) -> Result<TcpStream> {
    match route {
        ProxyRoute::Tor => {
            let stream = Socks5Stream::connect("127.0.0.1:9050", target).await
                .context("connecting through Tor SOCKS5; check that Tor is running")?;
            Ok(stream.into_inner())
        }
        ProxyRoute::SystemRoute => {
            Ok(TcpStream::connect(target).await.with_context(|| format!("connecting to {target}"))?)
        }
    }
}
