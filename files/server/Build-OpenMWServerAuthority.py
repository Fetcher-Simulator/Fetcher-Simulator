#!/usr/bin/env python3

#!/usr/bin/env python3

"""Build a portable OpenMW server content-authority bundle from a known-good client.

The multiplayer server does not consume a precomputed SHA manifest.  At startup it
loads the generated ``openmw.cfg`` and hashes the exact content/Lua bytes exposed by
that VFS.  This tool therefore builds the *input* to ServerContentRegistry:

    content-authority/
      openmw.cfg
      bundle-manifest.json
      content-data/

The builder intentionally treats mod files as opaque bytes.  It never decodes and
rewrites Lua or content files, so UTF-8 BOMs and CRLF/LF differences are preserved.

Typical usage on a known-good, fully updated OpenMW client install::

    python Build-OpenMWServerAuthority.py \
        --client-root C:\\Games\\OpenMW-MP \
        --output C:\\OpenMW-Server\\content-authority

By default the generated ``openmw.cfg`` assumes it will ultimately live at
``<server-root>/content-authority/openmw.cfg`` and that the matching server build has
``<server-root>/resources/vfs-mw`` available. Use ``--server-vfs-mw`` to write a
different server-side path for system-wide installations. The client and server
distributions must come from compatible builds so their built-in resources agree.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import sys
import uuid


CONTENT_EXTENSIONS = {".esm", ".esp", ".omwgame", ".omwaddon", ".omwscripts"}
BINARY_CONTENT_EXTENSIONS = {".esm", ".esp", ".omwgame", ".omwaddon"}
MODEL_SUBRECORDS = {b"MODL", b"MOD2", b"MOD3", b"MOD4", b"MOD5"}
ICON_SUBRECORDS = {b"ITEX", b"ICON"}
# DynamicRecordService currently accepts item definitions for these TES3 record types.
# Their model/icon assets need to be present in the authority VFS for runtime record validation.
RUNTIME_ITEM_RECORDS = {b"ALCH", b"WEAP", b"ARMO", b"CLOT", b"BOOK"}

# ServerCollisionWorld builds authoritative LOS/world collision from these placed record types.
# Do not copy every model definition: collect final definitions by record ID, then include only
# loose models whose IDs are actually referenced by CELL records in the configured content stack.
COLLISION_BLOCKER_RECORDS = {b"ACTI", b"CONT", b"DOOR", b"LIGH", b"STAT"}


@dataclass(frozen=True)
class DataRoot:
    path: Path
    external_server_resource: bool = False


@dataclass(frozen=True)
class VfsFile:
    logical_path: str
    physical_path: Path
    root: DataRoot
    root_index: int


@dataclass
class ParsedOpenMwConfig:
    path: Path
    data_roots: list[Path] = field(default_factory=list)
    data_local: Path | None = None
    fallback_archives: list[str] = field(default_factory=list)
    fallbacks: list[str] = field(default_factory=list)
    content_files: list[str] = field(default_factory=list)


@dataclass
class CopyRecord:
    logical_path: str
    source_path: str
    sha256: str
    size: int
    categories: set[str] = field(default_factory=set)


class BuildError(RuntimeError):
    pass


def unquote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
        return value[1:-1]
    return value


def normalize_vfs_path(value: str) -> str:
    """Return a safe relative VFS path using forward slashes."""

    normalized = value.replace("\\", "/").strip()
    while normalized.startswith("./"):
        normalized = normalized[2:]
    path = PurePosixPath(normalized)
    if (
        not normalized
        or path.is_absolute()
        or ":" in normalized
        or not path.parts
        or any(part in {"", ".", ".."} for part in path.parts)
    ):
        raise ValueError(f"invalid VFS-relative path: {value!r}")
    return path.as_posix()


def config_path(value: str, base: Path) -> Path:
    expanded = os.path.expandvars(os.path.expanduser(unquote(value)))
    path = Path(expanded)
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def parse_openmw_cfg(path: Path) -> ParsedOpenMwConfig:
    path = path.resolve(strict=True)
    result = ParsedOpenMwConfig(path=path)
    seen_content: set[str] = set()

    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8-sig").splitlines(), start=1
    ):
        line = raw_line.strip()
        if not line or line.startswith(("#", ";")):
            continue
        if "=" not in line:
            continue
        key, raw_value = line.split("=", 1)
        key = key.strip().casefold()
        value = unquote(raw_value)
        if key == "data":
            if value:
                result.data_roots.append(config_path(value, path.parent))
        elif key == "data-local":
            if value:
                result.data_local = config_path(value, path.parent)
        elif key == "fallback-archive":
            if value:
                result.fallback_archives.append(value)
        elif key == "fallback":
            if value:
                result.fallbacks.append(value)
        elif key == "content":
            if not value:
                continue
            folded = value.casefold()
            if folded in seen_content:
                raise BuildError(
                    f"duplicate content= entry at {path}:{line_number}: {value}"
                )
            seen_content.add(folded)
            result.content_files.append(value)

    if not result.content_files:
        raise BuildError(f"no content= entries found in {path}")
    return result


def same_path(left: Path, right: Path) -> bool:
    try:
        return left.samefile(right)
    except (FileNotFoundError, OSError):
        return os.path.normcase(str(left.resolve())) == os.path.normcase(str(right.resolve()))


class VfsIndex:
    """Case-insensitive loose-file view with OpenMW data-root precedence."""

    def __init__(self, roots: list[DataRoot]) -> None:
        self.roots = roots
        self.files: dict[str, VfsFile] = {}
        self.file_count = 0
        self._build()

    def _build(self) -> None:
        for root_index, root in enumerate(self.roots):
            current_root: dict[str, VfsFile] = {}
            for dirpath, dirnames, filenames in os.walk(root.path):
                dirnames.sort(key=str.casefold)
                filenames.sort(key=str.casefold)
                directory = Path(dirpath)
                for filename in filenames:
                    physical = directory / filename
                    try:
                        relative = physical.relative_to(root.path).as_posix()
                        logical = normalize_vfs_path(relative)
                    except ValueError as error:
                        raise BuildError(f"unsafe path below data root {root.path}: {physical}") from error
                    key = logical.casefold()
                    if key in current_root:
                        other = current_root[key].physical_path
                        raise BuildError(
                            "ambiguous case-insensitive VFS path in one data root: "
                            f"{other} and {physical}"
                        )
                    current_root[key] = VfsFile(logical, physical, root, root_index)
                    self.file_count += 1
            # OpenMW data= entries are ordered from lower to higher priority.
            self.files.update(current_root)

    def get(self, logical_path: str) -> VfsFile | None:
        try:
            key = normalize_vfs_path(logical_path).casefold()
        except ValueError:
            return None
        return self.files.get(key)

    def effective_by_suffix(self, suffix: str) -> list[VfsFile]:
        suffix = suffix.casefold()
        return sorted(
            (
                entry
                for entry in self.files.values()
                if PurePosixPath(entry.logical_path).suffix.casefold() == suffix
            ),
            key=lambda entry: entry.logical_path.casefold(),
        )

    def effective_under(self, top_level: str) -> list[VfsFile]:
        prefix = normalize_vfs_path(top_level).casefold().rstrip("/") + "/"
        return sorted(
            (
                entry
                for key, entry in self.files.items()
                if key.startswith(prefix)
            ),
            key=lambda entry: entry.logical_path.casefold(),
        )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_file_atomically(source: Path, destination: Path) -> tuple[str, int]:
    """Binary-copy source while hashing exactly the bytes written."""

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + f".{uuid.uuid4().hex}.tmp")
    digest = hashlib.sha256()
    size = 0
    try:
        with source.open("rb") as src, temporary.open("xb") as dst:
            for chunk in iter(lambda: src.read(1024 * 1024), b""):
                digest.update(chunk)
                size += len(chunk)
                dst.write(chunk)
        os.replace(temporary, destination)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
    return digest.hexdigest(), size


def decode_subrecord_string(value: bytes, encoding: str) -> str:
    value = value.split(b"\0", 1)[0]
    if not value:
        return ""
    return value.decode(encoding, errors="replace").strip()


def collect_asset_references(
    path: Path, encoding: str
) -> tuple[set[str], set[str], dict[str, str], set[str]]:
    """Collect runtime-item assets plus placed server-collision model dependencies."""

    models: set[str] = set()
    icons: set[str] = set()
    blocker_models: dict[str, str] = {}
    placed_reference_ids: set[str] = set()

    with path.open("rb") as stream:
        record_index = 0
        while True:
            header = stream.read(16)
            if not header:
                break
            if len(header) != 16:
                raise ValueError(f"short record header at record {record_index}")
            record_type = header[:4]
            body_size = struct.unpack_from("<I", header, 4)[0]
            body = stream.read(body_size)
            if len(body) != body_size:
                raise ValueError(
                    f"short record body at record {record_index}: expected {body_size}, got {len(body)}"
                )

            interested = (
                record_type in RUNTIME_ITEM_RECORDS
                or record_type in COLLISION_BLOCKER_RECORDS
                or record_type == b"CELL"
            )
            if not interested:
                record_index += 1
                continue

            offset = 0
            blocker_id = ""
            blocker_model = ""
            waiting_for_reference_name = False
            while offset < len(body):
                if len(body) - offset < 8:
                    raise ValueError(
                        f"short subrecord header at record {record_index}, offset {offset}"
                    )
                name = body[offset : offset + 4]
                size = struct.unpack_from("<I", body, offset + 4)[0]
                start = offset + 8
                end = start + size
                if end > len(body):
                    raise ValueError(
                        f"short subrecord body {name!r} at record {record_index}, offset {offset}"
                    )
                value_bytes = body[start:end]

                if record_type in RUNTIME_ITEM_RECORDS:
                    if name in MODEL_SUBRECORDS:
                        value = decode_subrecord_string(value_bytes, encoding)
                        if value:
                            models.add(value)
                    elif name in ICON_SUBRECORDS:
                        value = decode_subrecord_string(value_bytes, encoding)
                        if value:
                            icons.add(value)
                elif record_type in COLLISION_BLOCKER_RECORDS:
                    if name == b"NAME" and not blocker_id:
                        blocker_id = decode_subrecord_string(value_bytes, encoding)
                    elif name in MODEL_SUBRECORDS and not blocker_model:
                        blocker_model = decode_subrecord_string(value_bytes, encoding)
                else:
                    if name == b"FRMR":
                        waiting_for_reference_name = True
                    elif waiting_for_reference_name and name == b"NAME":
                        reference_id = decode_subrecord_string(value_bytes, encoding)
                        if reference_id:
                            placed_reference_ids.add(reference_id.casefold())
                        waiting_for_reference_name = False

                offset = end

            if record_type in COLLISION_BLOCKER_RECORDS and blocker_id:
                # Content order determines the effective definition. Preserve an empty model so a
                # later deletion/override without MODL clears an earlier blocker-model definition.
                blocker_models[blocker_id.casefold()] = blocker_model

            record_index += 1

    return models, icons, blocker_models, placed_reference_ids


def model_candidates(reference: str) -> list[str]:
    try:
        normalized = normalize_vfs_path(reference)
    except ValueError:
        return []
    candidates = [normalized]
    prefixed = f"meshes/{normalized}"
    if prefixed.casefold() != normalized.casefold():
        candidates.append(prefixed)
    return candidates


def _replace_suffix(path: str, suffix: str) -> str:
    pure = PurePosixPath(path)
    if pure.suffix:
        return pure.with_suffix(suffix).as_posix()
    return path


def icon_candidates(reference: str) -> list[str]:
    """Approximate ResourceHelpers::correctIconPath search order."""

    try:
        normalized = normalize_vfs_path(reference)
    except ValueError:
        return []
    candidates: list[str] = [normalized]
    parts = list(PurePosixPath(normalized).parts)
    icon_index = next((i for i, part in enumerate(parts) if part.casefold() == "icons"), None)
    if icon_index is None:
        corrected = f"icons/{normalized}"
    else:
        corrected = PurePosixPath(*parts[icon_index:]).as_posix()

    corrected_dds = _replace_suffix(corrected, ".dds")
    for candidate in (
        corrected_dds,
        corrected,
        f"icons/{PurePosixPath(corrected_dds).name}",
        f"icons/{PurePosixPath(corrected).name}",
    ):
        if all(candidate.casefold() != existing.casefold() for existing in candidates):
            candidates.append(candidate)
    return candidates


def resolve_first(index: VfsIndex, candidates: list[str]) -> VfsFile | None:
    for candidate in candidates:
        result = index.get(candidate)
        if result is not None:
            return result
    return None


def quote_config_path(value: str) -> str:
    if not value or any(character in value for character in "\r\n\""):
        raise BuildError(f"invalid OpenMW configuration path: {value!r}")
    return f'"{value}"'


def build_generated_cfg(parsed: ParsedOpenMwConfig, server_vfs_mw: str) -> str:
    lines = [
        "# Generated by Build-OpenMWServerAuthority.py; do not edit by hand.",
        "# ServerContentRegistry hashes the exact VFS bytes exposed by this configuration.",
        f"data={quote_config_path(server_vfs_mw)}",
        'data="./content-data"',
    ]
    lines.extend(f"fallback-archive={value}" for value in parsed.fallback_archives)
    lines.extend(f"fallback={value}" for value in parsed.fallbacks)
    lines.extend(f"content={value}" for value in parsed.content_files)
    lines.append("")
    return "\n".join(lines)


def atomic_replace_directory(staging: Path, destination: Path, force: bool) -> None:
    if destination.exists() and not force:
        raise BuildError(f"output already exists; pass --force to replace it: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists():
        staging.replace(destination)
        return

    backup = destination.with_name(destination.name + f".old-{uuid.uuid4().hex}")
    destination.replace(backup)
    try:
        staging.replace(destination)
    except Exception:
        if not destination.exists() and backup.exists():
            backup.replace(destination)
        raise
    shutil.rmtree(backup)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Build a byte-exact portable content-authority directory for this OpenMW multiplayer server."
        )
    )
    parser.add_argument(
        "--client-root",
        required=True,
        type=Path,
        help="known-good OpenMW client root (contains openmw.cfg and resources/)",
    )
    parser.add_argument(
        "--openmw-cfg",
        type=Path,
        help="source OpenMW config; default: <client-root>/openmw.cfg",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="destination content-authority directory (required unless --plan-only)",
    )
    parser.add_argument(
        "--server-vfs-mw",
        default="../resources/vfs-mw",
        help=(
            "path written to the generated authority openmw.cfg for the server build's "
            "resources/vfs-mw directory (default: ../resources/vfs-mw)"
        ),
    )
    parser.add_argument(
        "--encoding",
        default="cp1252",
        help="TES3 string encoding used while scanning model/icon references (default: cp1252)",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="atomically replace an existing output directory after a successful build",
    )
    parser.add_argument(
        "--strict-assets",
        action="store_true",
        help="fail if a binary content file cannot be scanned for model/icon references",
    )
    parser.add_argument(
        "--include-sounds",
        action="store_true",
        help=(
            "also include every effective loose file under sound/; normally unnecessary for authority-only "
            "hosting, but useful if runtime dialogue records must validate loose audio"
        ),
    )
    parser.add_argument(
        "--plan-only",
        action="store_true",
        help="analyze the client VFS and print a summary without writing an authority bundle",
    )
    args = parser.parse_args()

    staging: Path | None = None
    try:
        client_root = args.client_root.resolve(strict=True)
        if not client_root.is_dir():
            raise BuildError(f"client root is not a directory: {client_root}")
        source_cfg = (args.openmw_cfg or (client_root / "openmw.cfg")).resolve(strict=True)
        parsed = parse_openmw_cfg(source_cfg)

        configured_roots = list(parsed.data_roots)
        if parsed.data_local is not None:
            configured_roots.append(parsed.data_local)

        builtin_vfs_mw = (client_root / "resources" / "vfs-mw").resolve()
        roots: list[DataRoot] = []
        missing_data_roots: list[str] = []
        for root in configured_roots:
            if not root.is_dir():
                missing_data_roots.append(str(root))
                continue
            roots.append(
                DataRoot(
                    path=root,
                    external_server_resource=builtin_vfs_mw.is_dir()
                    and same_path(root, builtin_vfs_mw),
                )
            )
        if not roots:
            raise BuildError("none of the configured data= directories exist")

        print(f"Indexing {len(roots)} existing OpenMW data roots...")
        index = VfsIndex(roots)
        print(f"Indexed {index.file_count} physical loose files; effective VFS entries={len(index.files)}")

        missing_content: list[str] = []
        content_entries: list[tuple[str, VfsFile]] = []
        for name in parsed.content_files:
            resolved = index.get(name)
            if resolved is None:
                missing_content.append(name)
            else:
                content_entries.append((name, resolved))
        if missing_content:
            raise BuildError(
                "configured content files are missing from the effective VFS: "
                + ", ".join(missing_content)
            )

        missing_archives: list[str] = []
        archive_entries: list[tuple[str, VfsFile]] = []
        for name in parsed.fallback_archives:
            resolved = index.get(name)
            if resolved is None:
                missing_archives.append(name)
            else:
                archive_entries.append((name, resolved))
        if missing_archives:
            raise BuildError(
                "configured fallback archives are missing from the effective VFS: "
                + ", ".join(missing_archives)
            )

        model_refs: set[str] = set()
        icon_refs: set[str] = set()
        blocker_models: dict[str, str] = {}
        placed_reference_ids: set[str] = set()
        parse_errors: list[dict[str, str]] = []
        for configured_name, resolved in content_entries:
            if PurePosixPath(configured_name).suffix.casefold() not in BINARY_CONTENT_EXTENSIONS:
                continue
            try:
                models, icons, content_blockers, content_references = collect_asset_references(
                    resolved.physical_path, args.encoding
                )
                model_refs.update(models)
                icon_refs.update(icons)
                blocker_models.update(content_blockers)
                placed_reference_ids.update(content_references)
            except (OSError, UnicodeError, ValueError) as error:
                parse_errors.append({"content": configured_name, "error": str(error)})
        if parse_errors and args.strict_assets:
            first = parse_errors[0]
            raise BuildError(
                f"failed to scan {len(parse_errors)} content file(s); first: "
                f"{first['content']}: {first['error']}"
            )

        resolved_models: dict[str, VfsFile] = {}
        unresolved_models: list[str] = []
        for reference in sorted(model_refs, key=str.casefold):
            resolved = resolve_first(index, model_candidates(reference))
            if resolved is None:
                unresolved_models.append(reference)
            else:
                resolved_models[resolved.logical_path.casefold()] = resolved

        resolved_icons: dict[str, VfsFile] = {}
        unresolved_icons: list[str] = []
        for reference in sorted(icon_refs, key=str.casefold):
            resolved = resolve_first(index, icon_candidates(reference))
            if resolved is None:
                unresolved_icons.append(reference)
            else:
                resolved_icons[resolved.logical_path.casefold()] = resolved

        collision_model_refs = {
            blocker_models[reference_id]
            for reference_id in placed_reference_ids
            if blocker_models.get(reference_id)
        }
        resolved_collision_models: dict[str, VfsFile] = {}
        unresolved_collision_models: list[str] = []
        for reference in sorted(collision_model_refs, key=str.casefold):
            resolved = resolve_first(index, model_candidates(reference))
            if resolved is None:
                unresolved_collision_models.append(reference)
            else:
                resolved_collision_models[resolved.logical_path.casefold()] = resolved

        lua_files = index.effective_by_suffix(".lua")
        sound_files = index.effective_under("sound") if args.include_sounds else []

        copy_plan: dict[str, tuple[str, VfsFile, set[str]]] = {}

        def plan_file(entry: VfsFile, category: str, logical_override: str | None = None) -> None:
            # Files supplied by the server distribution's resources/vfs-mw stay external.
            if entry.root.external_server_resource:
                return
            logical = normalize_vfs_path(logical_override or entry.logical_path)
            key = logical.casefold()
            existing = copy_plan.get(key)
            if existing is None:
                copy_plan[key] = (logical, entry, {category})
                return
            existing_logical, existing_entry, categories = existing
            if existing_entry.physical_path != entry.physical_path:
                raise BuildError(
                    f"two different physical files were selected for VFS path {logical}: "
                    f"{existing_entry.physical_path} and {entry.physical_path}"
                )
            # Preserve the configured spelling for top-level content/archive names.
            if logical_override is not None:
                existing_logical = logical
            categories.add(category)
            copy_plan[key] = (existing_logical, existing_entry, categories)

        for configured_name, entry in content_entries:
            plan_file(entry, "content", configured_name)
        for configured_name, entry in archive_entries:
            plan_file(entry, "archive", configured_name)
        for entry in lua_files:
            plan_file(entry, "lua")
        for entry in resolved_models.values():
            plan_file(entry, "model")
        for entry in resolved_icons.values():
            plan_file(entry, "icon")
        for entry in resolved_collision_models.values():
            plan_file(entry, "collision-model")
        for entry in sound_files:
            plan_file(entry, "sound")

        external_lua = sum(1 for entry in lua_files if entry.root.external_server_resource)
        summary = {
            "configured_content": len(parsed.content_files),
            "configured_archives": len(parsed.fallback_archives),
            "effective_lua_files": len(lua_files),
            "external_resource_lua_files": external_lua,
            "model_refs": len(model_refs),
            "resolved_loose_models": len(resolved_models),
            "unresolved_loose_models": len(unresolved_models),
            "icon_refs": len(icon_refs),
            "resolved_loose_icons": len(resolved_icons),
            "unresolved_loose_icons": len(unresolved_icons),
            "placed_reference_ids": len(placed_reference_ids),
            "collision_model_refs": len(collision_model_refs),
            "resolved_loose_collision_models": len(resolved_collision_models),
            "unresolved_loose_collision_models": len(unresolved_collision_models),
            "sound_files": len(sound_files),
            "planned_bundle_files": len(copy_plan),
            "parse_errors": len(parse_errors),
            "missing_data_roots": len(missing_data_roots),
        }
        print(json.dumps(summary, indent=2))
        if args.plan_only:
            return 0
        if args.output is None:
            raise BuildError("--output is required unless --plan-only is used")

        output = args.output.resolve()
        if output.exists() and not args.force:
            raise BuildError(f"output already exists; pass --force to replace it: {output}")
        output.parent.mkdir(parents=True, exist_ok=True)
        staging = output.with_name(output.name + f".tmp-{uuid.uuid4().hex}")
        staging.mkdir(parents=False)
        content_data = staging / "content-data"
        content_data.mkdir()

        copied_records: dict[str, CopyRecord] = {}
        total_bytes = 0
        ordered_plan = sorted(copy_plan.items(), key=lambda item: item[0])
        print(f"Copying {len(ordered_plan)} effective files byte-for-byte...")
        for number, (key, (logical, entry, categories)) in enumerate(ordered_plan, start=1):
            destination = content_data.joinpath(*PurePosixPath(logical).parts)
            digest, size = copy_file_atomically(entry.physical_path, destination)
            total_bytes += size
            copied_records[key] = CopyRecord(
                logical_path=logical,
                source_path=str(entry.physical_path),
                sha256=digest,
                size=size,
                categories=set(categories),
            )
            if number % 500 == 0 or number == len(ordered_plan):
                print(f"  copied {number}/{len(ordered_plan)} files ({total_bytes / (1024 * 1024):.1f} MiB)")

        generated_cfg = build_generated_cfg(parsed, args.server_vfs_mw)
        (staging / "openmw.cfg").write_text(
            generated_cfg, encoding="utf-8", newline="\n"
        )

        manifest_files = [
            {
                "path": record.logical_path,
                "sha256": record.sha256,
                "size": record.size,
                "categories": sorted(record.categories),
                "source": record.source_path,
            }
            for record in sorted(copied_records.values(), key=lambda record: record.logical_path.casefold())
        ]
        manifest = {
            "format_version": 2,
            "generated_at_utc": datetime.now(timezone.utc).isoformat(),
            "source_client_root": str(client_root),
            "source_openmw_cfg": str(source_cfg),
            "server_vfs_mw": args.server_vfs_mw,
            "content_count": len(parsed.content_files),
            "archive_count": len(parsed.fallback_archives),
            "model_refs": len(model_refs),
            "icon_refs": len(icon_refs),
            "placed_reference_ids": len(placed_reference_ids),
            "collision_model_refs": len(collision_model_refs),
            "loose_asset_files": sum(
                1
                for record in copied_records.values()
                if record.categories.intersection({"model", "icon", "collision-model", "sound"})
            ),
            "lua_files": len(lua_files),
            "copied_lua_files": sum(
                1 for record in copied_records.values() if "lua" in record.categories
            ),
            "missing_content": [],
            "missing_archives": [],
            "missing_data_roots": missing_data_roots,
            "parse_errors": parse_errors,
            "bytes": total_bytes,
            "unresolved_model_count": len(unresolved_models),
            "unresolved_icon_count": len(unresolved_icons),
            "unresolved_collision_model_count": len(unresolved_collision_models),
            "unresolved_models": unresolved_models,
            "unresolved_icons": unresolved_icons,
            "unresolved_collision_models": unresolved_collision_models,
            "summary": summary,
            "files": manifest_files,
        }
        (staging / "bundle-manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
            newline="\n",
        )

        atomic_replace_directory(staging, output, args.force)
        staging = None
        print(f"Built server content authority: {output}")
        print(f"  files={len(copied_records)} bytes={total_bytes} ({total_bytes / (1024 ** 3):.2f} GiB)")
        print(f"  config={output / 'openmw.cfg'}")
        print(f"  manifest={output / 'bundle-manifest.json'}")
        return 0
    except (BuildError, OSError, UnicodeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    finally:
        if staging is not None and staging.exists():
            shutil.rmtree(staging, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
