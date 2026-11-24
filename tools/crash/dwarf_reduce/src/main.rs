//! dwarf-reduce ELF INFO ABBREV
//!
//! Writes a `.debug_info` and `.debug_abbrev` for ELF that keep what PostHog's symbolicator (cymbal, through
//! symbolic's SymCacheConverter) reads: each unit's root DIE, and its subprogram and inlined-subroutine DIEs with
//! their names, origins, ranges and call sites. The other DWARF sections stay valid against the result, except
//! `.debug_aranges` and the name indexes, which hold unit offsets and must be dropped.

use gimli::constants::*;
use gimli::{AttributeValue, EndianSlice, LittleEndian, Section, SectionId, UnitType};
use object::{Object, ObjectSection};
use std::collections::HashMap;
use std::error::Error;

type Reader<'a> = EndianSlice<'a, LittleEndian>;
type Units<'a> = gimli::DebugInfoUnitHeadersIter<Reader<'a>>;

const FUNCTION_ATTRIBUTES: &[DwAt] = &[
    DW_AT_name,
    DW_AT_linkage_name,
    DW_AT_MIPS_linkage_name,
    DW_AT_abstract_origin,
    DW_AT_specification,
    DW_AT_low_pc,
    DW_AT_high_pc,
    DW_AT_ranges,
    DW_AT_call_file,
    DW_AT_call_line,
];

fn kept_tag(tag: DwTag, depth: isize) -> bool {
    depth == 0
        || tag == DW_TAG_subprogram
        || tag == DW_TAG_inlined_subroutine
        || tag == DW_TAG_imported_unit
}

fn kept_attribute(tag: DwTag, depth: isize, name: DwAt) -> bool {
    name != DW_AT_sibling
        && (depth == 0
            || FUNCTION_ATTRIBUTES.contains(&name)
            || (tag == DW_TAG_imported_unit && name == DW_AT_import))
}

#[derive(Hash, PartialEq, Eq, Clone)]
struct Abbreviation {
    tag: u16,
    children: bool,
    attributes: Vec<(u16, u16, i64)>,
}

struct Node {
    depth: usize,
    old: u32,
    abbreviation: Abbreviation,
    bytes: Vec<u8>,
    references: Vec<(usize, u32, bool)>,
}

struct Output {
    info: Vec<u8>,
    abbreviations: HashMap<Abbreviation, u64>,
    order: Vec<Abbreviation>,
    old_offsets: Vec<u32>,
    new_offsets: Vec<u32>,
    fixups: Vec<(usize, u32, Option<u32>)>,
}

impl Output {
    fn code(&mut self, abbreviation: Abbreviation) -> u64 {
        if let Some(&code) = self.abbreviations.get(&abbreviation) {
            return code;
        }
        self.order.push(abbreviation.clone());
        let code = self.order.len() as u64;
        self.abbreviations.insert(abbreviation, code);
        code
    }

    fn abbrev_section(&self) -> Vec<u8> {
        let mut out = Vec::new();
        for (index, abbreviation) in self.order.iter().enumerate() {
            uleb(&mut out, index as u64 + 1);
            uleb(&mut out, abbreviation.tag.into());
            out.push(abbreviation.children.into());
            for &(name, form, value) in &abbreviation.attributes {
                uleb(&mut out, name.into());
                uleb(&mut out, form.into());
                if form == DW_FORM_implicit_const.0 {
                    sleb(&mut out, value);
                }
            }
            out.extend_from_slice(&[0, 0]);
        }
        out.push(0);
        out
    }
}

fn uleb(out: &mut Vec<u8>, mut value: u64) {
    loop {
        let byte = (value & 0x7f) as u8;
        value >>= 7;
        if value == 0 {
            out.push(byte);
            return;
        }
        out.push(byte | 0x80);
    }
}

fn sleb(out: &mut Vec<u8>, mut value: i64) {
    loop {
        let byte = (value & 0x7f) as u8;
        value >>= 7;
        if (value == 0 && byte & 0x40 == 0) || (value == -1 && byte & 0x40 != 0) {
            out.push(byte);
            return;
        }
        out.push(byte | 0x80);
    }
}

fn offset32(value: usize) -> Result<u32, Box<dyn Error>> {
    u32::try_from(value).map_err(|_| "DWARF over 4 GiB".into())
}

fn kept_offsets(
    dwarf: &gimli::Dwarf<Reader>,
    mut units: Units,
) -> Result<Vec<u32>, Box<dyn Error>> {
    let mut kept = Vec::new();
    while let Some(header) = units.next()? {
        if matches!(
            header.type_(),
            UnitType::Type { .. } | UnitType::SplitType { .. }
        ) {
            continue;
        }
        let start = header.offset().0;
        let abbreviations = header.abbreviations(&dwarf.debug_abbrev)?;
        let mut entries = header.entries_raw(&abbreviations, None)?;
        while !entries.is_empty() {
            let at = entries.next_offset().0;
            let depth = entries.next_depth();
            let Some(abbreviation) = entries.read_abbreviation()? else {
                continue;
            };
            if kept_tag(abbreviation.tag(), depth) {
                kept.push(offset32(start + at)?);
            }
            entries.skip_attributes(abbreviation.attributes())?;
        }
    }
    Ok(kept)
}

fn reduce(
    dwarf: &gimli::Dwarf<Reader>,
    info: &[u8],
    kept: &[u32],
) -> Result<Output, Box<dyn Error>> {
    let mut out = Output {
        info: Vec::with_capacity(info.len() / 3),
        abbreviations: HashMap::new(),
        order: Vec::new(),
        old_offsets: Vec::with_capacity(kept.len()),
        new_offsets: Vec::with_capacity(kept.len()),
        fixups: Vec::new(),
    };
    let mut units = dwarf.debug_info.units();
    while let Some(header) = units.next()? {
        if header.format() != gimli::Format::Dwarf32 {
            return Err("64-bit DWARF is not supported".into());
        }
        if matches!(
            header.type_(),
            UnitType::Type { .. } | UnitType::SplitType { .. }
        ) {
            continue;
        }
        let start = header.offset().0;
        let abbreviations = header.abbreviations(&dwarf.debug_abbrev)?;
        let mut entries = header.entries_raw(&abbreviations, None)?;
        let mut nodes: Vec<Node> = Vec::new();
        let mut ancestors: Vec<isize> = Vec::new();
        while !entries.is_empty() {
            let at = entries.next_offset().0;
            let depth = entries.next_depth();
            let Some(abbreviation) = entries.read_abbreviation()? else {
                continue;
            };
            let tag = abbreviation.tag();
            while ancestors.last().is_some_and(|&d| d >= depth) {
                ancestors.pop();
            }
            if !kept_tag(tag, depth) {
                entries.skip_attributes(abbreviation.attributes())?;
                continue;
            }
            let mut node = Node {
                depth: ancestors.len(),
                old: offset32(start + at)?,
                abbreviation: Abbreviation {
                    tag: tag.0,
                    children: false,
                    attributes: Vec::new(),
                },
                bytes: Vec::new(),
                references: Vec::new(),
            };
            ancestors.push(depth);
            for spec in abbreviation.attributes() {
                let from = entries.next_offset().0;
                let attribute = entries.read_attribute(*spec)?;
                let to = entries.next_offset().0;
                if !kept_attribute(tag, depth, spec.name()) {
                    continue;
                }
                let (target, local) = match attribute.raw_value() {
                    AttributeValue::UnitRef(o) => (Some(offset32(start + o.0)?), true),
                    AttributeValue::DebugInfoRef(o) => (Some(offset32(o.0)?), false),
                    AttributeValue::DebugTypesRef(_) | AttributeValue::DebugInfoRefSup(_) => {
                        continue
                    }
                    _ => (None, false),
                };
                match target {
                    Some(target) if kept.binary_search(&target).is_err() => {}
                    Some(target) => {
                        let form = if local {
                            DW_FORM_ref4
                        } else {
                            DW_FORM_ref_addr
                        };
                        node.abbreviation
                            .attributes
                            .push((spec.name().0, form.0, 0));
                        node.references.push((node.bytes.len(), target, local));
                        node.bytes.extend_from_slice(&[0; 4]);
                    }
                    None => {
                        let constant = spec.implicit_const_value().unwrap_or(0);
                        node.abbreviation
                            .attributes
                            .push((spec.name().0, spec.form().0, constant));
                        node.bytes
                            .extend_from_slice(&info[start + from..start + to]);
                    }
                }
            }
            nodes.push(node);
        }
        let unit = out.info.len();
        out.info
            .extend_from_slice(&info[start..start + header.header_size()]);
        let abbrev_offset = unit + if header.version() >= 5 { 8 } else { 6 };
        out.info[abbrev_offset..abbrev_offset + 4].copy_from_slice(&0u32.to_le_bytes());
        for i in 0..nodes.len() {
            let next_depth = nodes.get(i + 1).map_or(0, |n| n.depth);
            let node = &mut nodes[i];
            node.abbreviation.children = next_depth > node.depth;
            let code = out.code(node.abbreviation.clone());
            out.old_offsets.push(node.old);
            out.new_offsets.push(offset32(out.info.len())?);
            uleb(&mut out.info, code);
            let base = out.info.len();
            for &(at, target, local) in &node.references {
                out.fixups
                    .push((base + at, target, local.then_some(unit as u32)));
            }
            out.info.extend_from_slice(&node.bytes);
            if !node.abbreviation.children {
                out.info.extend(std::iter::repeat_n(
                    0u8,
                    node.depth - next_depth.min(node.depth),
                ));
            }
        }
        let length = offset32(out.info.len() - unit - 4)?;
        out.info[unit..unit + 4].copy_from_slice(&length.to_le_bytes());
    }
    for &(at, target, unit) in &out.fixups {
        let index = out
            .old_offsets
            .binary_search(&target)
            .map_err(|_| "a kept reference lost its target")?;
        let value = out.new_offsets[index] - unit.unwrap_or(0);
        out.info[at..at + 4].copy_from_slice(&value.to_le_bytes());
    }
    Ok(out)
}

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = std::env::args().collect();
    let [_, input, info_path, abbrev_path] = args.as_slice() else {
        return Err("usage: dwarf-reduce ELF INFO ABBREV".into());
    };
    let file = std::fs::File::open(input)?;
    // SAFETY: the input is a file this process only reads, and nothing else writes it while it runs.
    let map = unsafe { memmap2::Mmap::map(&file)? };
    let elf = object::File::parse(&*map)?;
    if elf.section_by_name(".debug_info").is_some_and(|s| {
        s.compressed_file_range()
            .is_ok_and(|r| r.format != object::CompressionFormat::None)
    }) {
        return Err("decompress the debug sections first".into());
    }
    let section = |id: SectionId| -> Result<Reader, gimli::Error> {
        let data = elf
            .section_by_name(id.name())
            .and_then(|s| s.data().ok())
            .unwrap_or(&[]);
        Ok(EndianSlice::new(data, LittleEndian))
    };
    let dwarf = gimli::Dwarf::load(section)?;
    let info = dwarf.debug_info.reader().slice();
    let kept = kept_offsets(&dwarf, dwarf.debug_info.units())?;
    let out = reduce(&dwarf, info, &kept)?;
    std::fs::write(info_path, &out.info)?;
    std::fs::write(abbrev_path, out.abbrev_section())?;
    eprintln!(
        "dwarf-reduce: .debug_info {} -> {} bytes, {} DIEs kept",
        info.len(),
        out.info.len(),
        kept.len()
    );
    Ok(())
}
