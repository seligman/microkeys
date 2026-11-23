#!/usr/bin/env python3

# Package MicroKeys for release: one zip per architecture plus the update
# feed's latest.json, optionally published to S3.
#
# Nothing about the feed lives in this public repo.  The feed URL, the S3
# location, and the AWS profile and region come from ~/.microkeys_release.json,
# or from the MICROKEYS_FEED_URL, MICROKEYS_S3_URL, MICROKEYS_AWS_PROFILE and
# MICROKEYS_AWS_REGION environment variables.  The file looks like:
#
#   {
#       "feed_url": "https://<bucket>.s3.amazonaws.com/<prefix>/",
#       "s3_url": "s3://<bucket>/<prefix>/",
#       "aws_profile": "<profile, optional>",
#       "region": "<region, optional>"
#   }
#
# Without a feed URL the zips are built without one, and those copies never
# look for updates.
#
# Publishing works like Notepad 2's deploy.py: every object is public-read;
# the zips are immutable (one per version and architecture, never replaced
# with other bytes); versions.json keeps every manifest published; and
# latest.json, which the app polls, goes last so it never names a file that
# isn't there yet.
#
# Usage:
#   make_release.py [--arch x64,arm64] [--build] [--notes "..."]   package into dist/
#   make_release.py ... --upload                                     package, then publish
#   make_release.py --upload-only [--dist <dir>]                     publish what's already in dist/
# Add --dry-run to see what publishing would do, --force to publish an older version.

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CONFIG = os.path.join(os.path.expanduser("~"), ".microkeys_release.json")
IMMUTABLE = "public, max-age=31536000, immutable"


def read_version():
    with open(os.path.join(ROOT, "src", "version.h")) as f:
        return re.search(r'VERSION_TEXT\s+"([^"]+)"', f.read()).group(1)


def version_key(text):
    parts = [int(x) for x in re.findall(r"\d+", text)[:3]]
    return parts + [0] * (3 - len(parts))


def load_config():
    config = {}
    if os.path.isfile(CONFIG):
        with open(CONFIG, encoding="utf-8") as f:
            config = json.load(f)
    for key, env in (("feed_url", "MICROKEYS_FEED_URL"), ("s3_url", "MICROKEYS_S3_URL"),
                     ("aws_profile", "MICROKEYS_AWS_PROFILE"), ("region", "MICROKEYS_AWS_REGION")):
        if os.environ.get(env):
            config[key] = os.environ[env]
    for key in ("feed_url", "s3_url"):
        if config.get(key) and not config[key].endswith("/"):
            config[key] += "/"
    return config


def run(cmd):
    print("$ " + " ".join(f'"{x}"' if " " in x else x for x in cmd))
    subprocess.check_call(cmd)


def build(arch):
    platform = "ARM64" if arch == "arm64" else "x64"
    build_dir = os.path.join(ROOT, "build", arch)
    run(["cmake", "-S", ROOT, "-B", build_dir, "-G", "Visual Studio 17 2022", "-A", platform])
    run(["cmake", "--build", build_dir, "--config", "Release", "--", "/m", "/v:minimal"])


def sha256_file(fn):
    h = hashlib.sha256()
    with open(fn, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def package(arch, version, config, dist):
    # Copy the staged build, add defaults.json with the feed, and zip it with
    # a MicroKeys/ folder at the top, which is what the updater expects
    stage = os.path.join(ROOT, "build", arch, "stage", "Release", "MicroKeys")
    app = f"app-{version}"
    if not os.path.isfile(os.path.join(stage, app, "MicroKeysApp.exe")) or not os.path.isfile(os.path.join(stage, "MicroKeys.exe")):
        raise SystemExit(f"No {arch} build of {version} in {stage}; build it first or pass --build")

    zip_name = f"MicroKeys-{version}-{arch}.zip"
    zip_path = os.path.join(dist, zip_name)
    defaults = {}
    if config.get("feed_url"):
        defaults["update_feed"] = config["feed_url"]

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(os.path.join(stage, "MicroKeys.exe"), "MicroKeys/MicroKeys.exe")
        app_dir = os.path.join(stage, app)
        for dirpath, dirnames, filenames in os.walk(app_dir):
            dirnames[:] = sorted(d for d in dirnames if d != "__pycache__")
            for name in sorted(filenames):
                if name in ("defaults.json",) or name.endswith(".pdb"):
                    continue
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, app_dir).replace(os.sep, "/")
                zf.write(full, f"MicroKeys/{app}/{rel}")
        if defaults:
            zf.writestr(f"MicroKeys/{app}/defaults.json", json.dumps(defaults, indent=4) + "\n")

    return {
        "zip": zip_path,
        "name": zip_name,
        "size": os.path.getsize(zip_path),
        "sha256": sha256_file(zip_path),
    }


class S3:
    # The few S3 calls publishing needs, through the AWS CLI
    def __init__(self, s3_url, profile, region):
        m = re.match(r"^s3://([^/]+)/(.*)$", s3_url)
        if not m:
            raise SystemExit(f"Bad s3_url: {s3_url}")
        self.bucket, self.prefix = m.group(1), m.group(2)
        self.extra = (["--profile", profile] if profile else []) + (["--region", region] if region else [])

    def _run(self, args):
        return subprocess.run(["aws"] + args + self.extra, capture_output=True, text=True)

    def head(self, name):
        # None when missing, else (size, sha256 metadata)
        r = self._run(["s3api", "head-object", "--bucket", self.bucket, "--key", self.prefix + name, "--output", "json"])
        if r.returncode != 0:
            if "(404)" in r.stderr or "Not Found" in r.stderr:
                return None
            raise SystemExit(f"Could not check {name}:\n{r.stderr}")
        info = json.loads(r.stdout or "{}")
        return int(info.get("ContentLength", -1)), (info.get("Metadata") or {}).get("sha256", "").lower()

    def get_text(self, name):
        # None when missing; any other failure stops, so a hiccup can't reset versions.json
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "obj")
            r = self._run(["s3api", "get-object", "--bucket", self.bucket, "--key", self.prefix + name, out])
            if r.returncode != 0:
                if "NoSuchKey" in r.stderr or "(404)" in r.stderr:
                    return None
                raise SystemExit(f"Could not read {name}:\n{r.stderr}")
            with open(out, encoding="utf-8") as f:
                return f.read()

    def put(self, name, path, content_type, cache_control, sha256=None):
        print(f"Uploading {name}")
        r = self._run(["s3api", "put-object", "--bucket", self.bucket, "--key", self.prefix + name, "--body", path,
                       "--acl", "public-read", "--content-type", content_type, "--cache-control", cache_control]
                      + (["--metadata", f"sha256={sha256}"] if sha256 else []))
        if r.returncode != 0:
            raise SystemExit(f"Upload of {name} failed:\n{r.stderr}")


def publish(config, dist, force, dry_run):
    # Zips, then versions.json, then latest.json
    with open(os.path.join(dist, "latest.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    version = manifest["version"]
    s3 = S3(config["s3_url"], config.get("aws_profile"), config.get("region"))
    print(f"Publishing MicroKeys {version} to s3://{s3.bucket}/{s3.prefix}")

    current = s3.get_text("latest.json")
    if current is not None:
        published = json.loads(current).get("version", "0")
        if version_key(published) > version_key(version) and not force:
            raise SystemExit(f"{published} is already published, which is newer than {version}; use --force to go back")

    zips = []
    for key in manifest:
        if key.startswith("url_"):
            arch = key[4:]
            name = manifest[key].split("/")[-1]
            path = os.path.join(dist, name)
            if sha256_file(path) != manifest[f"sha256_{arch}"]:
                raise SystemExit(f"{name} doesn't match latest.json; package it again")
            if not manifest[key].startswith(config["feed_url"]):
                raise SystemExit(f"latest.json points {arch} at {manifest[key]}, not the configured feed")
            zips.append((name, path, manifest[f"sha256_{arch}"]))
    if not zips:
        raise SystemExit("latest.json names no zips")

    for name, path, sha in zips:
        existing = s3.head(name)
        if existing is not None:
            if existing[1] == sha:
                print(f"{name} is already there")
                continue
            raise SystemExit(f"{name} is already published with other bytes; zips are never replaced, bump the version")
        if not dry_run:
            s3.put(name, path, "application/zip", IMMUTABLE, sha)

    versions = json.loads(s3.get_text("versions.json") or "[]")
    if not any(v.get("version") == version and v == manifest for v in versions):
        versions = [v for v in versions if v.get("version") != version] + [manifest]
    with tempfile.TemporaryDirectory() as d:
        fn = os.path.join(d, "versions.json")
        with open(fn, "w", encoding="utf-8") as f:
            json.dump(versions, f, indent=4)
            f.write("\n")
        if not dry_run:
            s3.put("versions.json", fn, "application/json", "no-cache")
            s3.put("latest.json", os.path.join(dist, "latest.json"), "application/json", "no-cache")
    print("Dry run, nothing uploaded" if dry_run else f"Published {version}: {config['feed_url']}latest.json")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", default="x64,arm64")
    parser.add_argument("--build", action="store_true", help="build each architecture first")
    parser.add_argument("--notes", default="", help="release notes shown in the update prompt")
    parser.add_argument("--upload", action="store_true", help="package, then publish")
    parser.add_argument("--upload-only", action="store_true", help="publish what's already packaged in --dist")
    parser.add_argument("--dist", default=os.path.join(ROOT, "dist"))
    parser.add_argument("--dry-run", action="store_true", help="show what publishing would do")
    parser.add_argument("--force", action="store_true", help="publish even if a newer version is already out")
    args = parser.parse_args()

    config = load_config()
    if (args.upload or args.upload_only) and not (config.get("feed_url") and config.get("s3_url")):
        raise SystemExit(f"Publishing needs feed_url and s3_url in {CONFIG} or the environment")

    if not args.upload_only:
        version = read_version()
        archs = [x.strip() for x in args.arch.split(",") if x.strip()]
        os.makedirs(args.dist, exist_ok=True)
        manifest = {"version": version}
        for arch in archs:
            if args.build:
                build(arch)
            info = package(arch, version, config, args.dist)
            manifest[f"url_{arch}"] = config.get("feed_url", "") + info["name"]
            manifest[f"sha256_{arch}"] = info["sha256"]
            manifest[f"size_{arch}"] = info["size"]
            print(f"Created {info['zip']}: {info['size']:,} bytes, SHA-256 {info['sha256']}")
        if args.notes:
            manifest["notes"] = args.notes
        latest = os.path.join(args.dist, "latest.json")
        with open(latest, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=4)
            f.write("\n")
        print(f"Created {latest}")
        if not config.get("feed_url"):
            print("No feed URL configured: these zips won't look for updates")

    if args.upload or args.upload_only:
        publish(config, args.dist, args.force, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
