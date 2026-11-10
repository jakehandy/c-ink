// Compiles the device's own renderer (firmware/components/cink_core) into the
// bridge, so the desktop preview is the exact frame the TICKEY will draw.
use std::path::PathBuf;

fn main() {
    let core = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../firmware/components/cink_core");
    let srcs = ["canvas", "font", "icons", "json", "model", "rank", "timefmt", "ui", "clawd", "capi"];
    let mut b = cc::Build::new();
    b.cpp(true).include(core.join("include")).warnings(false);
    if b.get_compiler().is_like_msvc() {
        b.flag("/std:c++17");
    } else {
        b.flag("-std=c++17");
    }
    for s in srcs {
        let p = core.join("src").join(format!("{s}.cpp"));
        println!("cargo:rerun-if-changed={}", p.display());
        b.file(p);
    }
    println!("cargo:rerun-if-changed={}", core.join("src/fonts_data.inc").display());
    println!("cargo:rerun-if-changed={}", core.join("include").display());
    b.compile("cink_core");
}
