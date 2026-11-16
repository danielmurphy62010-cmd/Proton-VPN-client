fn main() {
    let target_os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if target_os == "windows" {
        let mut resource = winres::WindowsResource::new();
        resource.set_manifest_file("proton-vpn-client.manifest");
        resource.compile().expect("failed to embed the Windows elevation manifest");
    }
}
