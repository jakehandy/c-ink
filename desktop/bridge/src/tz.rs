//! The device wants a POSIX TZ string ("PST8PDT,M3.2.0,M11.1.0"); desktops
//! know IANA names ("America/Los_Angeles"). Table from nayarsystems/posix_tz_db (MIT).

use std::collections::HashMap;
use std::sync::OnceLock;

static ZONES: &str = include_str!("../data/zones.json");

fn table() -> &'static HashMap<String, String> {
    static T: OnceLock<HashMap<String, String>> = OnceLock::new();
    T.get_or_init(|| serde_json::from_str(ZONES).unwrap_or_default())
}

pub fn iana() -> String {
    iana_time_zone::get_timezone().unwrap_or_else(|_| "UTC".into())
}

pub fn posix(iana: &str) -> String {
    table().get(iana).cloned().unwrap_or_else(|| "UTC0".into())
}

#[cfg(test)]
mod tests {
    #[test]
    fn known_zones() {
        assert_eq!(super::posix("America/Los_Angeles"), "PST8PDT,M3.2.0,M11.1.0");
        assert_eq!(super::posix("Nowhere/Land"), "UTC0");
    }
}
