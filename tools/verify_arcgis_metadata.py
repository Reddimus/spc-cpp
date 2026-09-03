#!/usr/bin/env python3
"""Compare NOAA's live ArcGIS layer metadata with the checked contract."""

from __future__ import annotations

import json
from pathlib import Path
import sys
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "tests" / "fixtures" / "arcgis_layers_2026-09-03.json"


def fetch_json(url: str) -> dict[str, object]:
    request = Request(url, headers={"User-Agent": "spc-cpp metadata check"})
    with urlopen(request, timeout=20) as response:
        return json.load(response)


def normalized_layers(document: dict[str, object]) -> list[list[object]]:
    layers = document.get("layers")
    if not isinstance(layers, list):
        raise ValueError("metadata response has no layers array")
    result: list[list[object]] = []
    for layer in layers:
        if not isinstance(layer, dict):
            raise ValueError("metadata layer is not an object")
        result.append([layer.get("id"), layer.get("name"), layer.get("parentLayerId")])
    return result


def main() -> int:
    contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
    failures: list[str] = []
    for service in contract["services"]:
        try:
            live = fetch_json(service["url"])
        except (OSError, URLError, TimeoutError, json.JSONDecodeError) as error:
            failures.append(f"{service['name']}: fetch failed: {error}")
            continue
        if live.get("currentVersion") != service["currentVersion"]:
            failures.append(
                f"{service['name']}: version {live.get('currentVersion')} != "
                f"{service['currentVersion']}"
            )
        if normalized_layers(live) != service["layers"]:
            failures.append(f"{service['name']}: layer ids, names, or parents changed")
            continue
        service_url = service["url"].split("?", 1)[0]
        query = urlencode(
            {
                "where": "1=1",
                "outFields": "*",
                "returnGeometry": "false",
                "resultRecordCount": "1",
                "f": "json",
            }
        )
        for layer_id in service["queryable"]:
            query_url = f"{service_url}/{layer_id}/query?{query}"
            try:
                response = fetch_json(query_url)
            except (OSError, URLError, TimeoutError, json.JSONDecodeError) as error:
                failures.append(f"{service['name']} layer {layer_id}: query failed: {error}")
                continue
            if "error" in response or not isinstance(response.get("features"), list):
                failures.append(
                    f"{service['name']} layer {layer_id}: query returned "
                    f"{response.get('error', 'no features array')}"
                )

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("NOAA ArcGIS metadata and all 39 feature layers match the 2026-09-03 contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
