use std::path::Path;
use std::process::Command;
use symbolic::common::ByteView;
use symbolic::debuginfo::Object;
use symbolic::symcache::SymCacheConverter;

fn run(command: &mut Command) {
    let status = command
        .status()
        .unwrap_or_else(|e| panic!("{command:?}: {e}"));
    assert!(status.success(), "{command:?}: {status}");
}

fn symcache(path: &Path) -> Vec<u8> {
    let view = ByteView::open(path).unwrap();
    let object = Object::parse(&view).unwrap();
    let mut converter = SymCacheConverter::new();
    converter.process_object(&object).unwrap();
    let mut out = Vec::new();
    converter
        .serialize(&mut std::io::Cursor::new(&mut out))
        .unwrap();
    out
}

fn section_size(path: &Path, name: &str) -> u64 {
    let data = std::fs::read(path).unwrap();
    let file = object::File::parse(&*data).unwrap();
    use object::{Object as _, ObjectSection as _};
    file.section_by_name(name).map_or(0, |s| s.size())
}

#[test]
fn reduced_debug_info_gives_the_same_symcache() {
    let dir = Path::new(env!("CARGO_TARGET_TMPDIR")).join("symcache");
    std::fs::create_dir_all(&dir).unwrap();
    let source = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/fixture.cpp");
    let program = dir.join("program");
    let full = dir.join("full.debug");
    let reduced = dir.join("reduced.debug");
    let (info, abbrev) = (dir.join("info.bin"), dir.join("abbrev.bin"));
    run(Command::new("g++")
        .args(["-std=c++17", "-O2", "-g", "-Wl,--build-id", "-o"])
        .arg(&program)
        .arg(&source));
    run(Command::new("objcopy")
        .arg("--only-keep-debug")
        .arg("--decompress-debug-sections")
        .arg(&program)
        .arg(&full));
    run(Command::new(env!("CARGO_BIN_EXE_dwarf-reduce"))
        .arg(&full)
        .arg(&info)
        .arg(&abbrev));
    run(Command::new("objcopy")
        .arg(format!("--update-section=.debug_info={}", info.display()))
        .arg(format!(
            "--update-section=.debug_abbrev={}",
            abbrev.display()
        ))
        .args([
            "--remove-section=.debug_aranges",
            "--remove-section=.debug_loclists",
        ])
        .arg(&full)
        .arg(&reduced));

    assert!(section_size(&reduced, ".debug_info") * 2 < section_size(&full, ".debug_info"));
    let expected = symcache(&full);
    assert!(expected.len() > 1000);
    assert!(
        expected == symcache(&reduced),
        "the reduced file's symcache differs from the full file's"
    );
}
