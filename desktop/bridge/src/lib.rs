//! C-Ink bridge: Claude Code in, sealed e-ink digests out.

pub mod claude;
pub mod claude_usage;
pub mod cloudflare;
pub mod device;
pub mod digest;
pub mod kinds;
pub mod news;
pub mod photo;
pub mod preview;
pub mod prompt;
pub mod relay;
pub mod seal;
pub mod server;
pub mod service;
pub mod settings;
pub mod tz;
pub mod weather;

pub use service::Bridge;
pub use settings::Settings;
