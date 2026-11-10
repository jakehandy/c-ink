//! LAN endpoint the TICKEY fetches from, advertised over mDNS as _cink._tcp.

use std::io::Cursor;

use anyhow::Result;
use mdns_sd::{ServiceDaemon, ServiceInfo};
use tiny_http::{Header, Request, Response, Server};

use crate::service::{host_name, Bridge};

fn header(k: &str, v: &str) -> Header {
    Header::from_bytes(k.as_bytes(), v.as_bytes()).unwrap()
}

fn bearer(r: &Request) -> Option<String> {
    r.headers()
        .iter()
        .find(|h| h.field.equiv("Authorization"))
        .and_then(|h| h.value.as_str().strip_prefix("Bearer ").map(str::to_string))
}

fn if_none_match(r: &Request) -> Option<String> {
    r.headers().iter().find(|h| h.field.equiv("If-None-Match")).map(|h| h.value.as_str().to_string())
}

fn respond(b: &Bridge, r: Request) {
    let url = r.url().to_string();
    let path = url.split('?').next().unwrap_or("");
    let reply: Response<Cursor<Vec<u8>>> = if path == "/v1/ping" {
        Response::from_data(format!("{{\"ok\":true,\"host\":\"{}\"}}", host_name()).into_bytes())
            .with_header(header("Content-Type", "application/json"))
    } else if let Some(id) = path.strip_prefix("/v1/digest/") {
        match b.device(id) {
            Some(d) if bearer(&r).as_deref() == Some(d.token.as_str()) => {
                b.touch_device(id);
                match b.sealed_for(&d) {
                    Some((etag, _)) if if_none_match(&r).as_deref() == Some(etag.as_str()) => {
                        Response::from_data(vec![]).with_status_code(304)
                    }
                    Some((etag, blob)) => Response::from_data(blob)
                        .with_header(header("Content-Type", "application/octet-stream"))
                        .with_header(header("ETag", &etag)),
                    None => Response::from_data(b"no digest yet".to_vec()).with_status_code(503),
                }
            }
            _ => Response::from_data(b"unauthorized".to_vec()).with_status_code(401),
        }
    } else {
        Response::from_data(b"not found".to_vec()).with_status_code(404)
    };
    let _ = r.respond(reply);
}

/// Binds the LAN endpoint (port 0 picks a free one) and returns the bound port.
pub fn spawn(b: Bridge, port: u16) -> Result<u16> {
    let server = Server::http(("0.0.0.0", port)).map_err(|e| anyhow::anyhow!("port {port}: {e}"))?;
    let port = server.server_addr().to_ip().map(|a| a.port()).unwrap_or(port);
    std::thread::Builder::new().name("cink-http".into()).spawn(move || {
        for r in server.incoming_requests() {
            respond(&b, r);
        }
    })?;
    advertise(port);
    Ok(port)
}

fn advertise(port: u16) {
    let Ok(mdns) = ServiceDaemon::new() else { return };
    let host = host_name();
    let local = format!("{}.local.", host.trim_end_matches(".local"));
    let props = [("v", "1")];
    if let Ok(info) = ServiceInfo::new("_cink._tcp.local.", &host, &local, "", port, &props[..]) {
        let _ = mdns.register(info.enable_addr_auto());
        // Keep the daemon alive for the life of the process.
        std::mem::forget(mdns);
    }
}
