# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alejandro Bustos
"""Check parsed Turtle against the C++ port ABI. Test dependency: rdflib."""
import pathlib
import re
import sys

try:
    import rdflib
    from rdflib import Namespace, RDF, RDFS, URIRef
except ImportError:
    sys.exit("FAIL ttl_check: install python3-rdflib (or pip install rdflib)")

ROOT = pathlib.Path(__file__).resolve().parents[1]
LV2 = Namespace("http://lv2plug.in/ns/lv2core#")
DOAP = Namespace("http://usefulinc.com/ns/doap#")
UNITS = Namespace("http://lv2plug.in/ns/extensions/units#")
DRIZZLE = URIRef("https://github.com/ABustos2606/drizzle#drizzle")
SYNTH = URIRef("https://github.com/ABustos2606/drizzle#synth")
QUALITY = {0: "Dirty tape", 1: "Clean tape", 2: "Vintage digital", 3: "Digital"}
# Fixed in milestone 0: symbols never change and new ports are appended.
GRAIN = """time size shape pitch density time_mod size_mod shape_mod pitch_mod time_uses_cv size_uses_cv
shape_uses_cv pitch_uses_cv"""
CVS = "time_cv size_cv shape_cv pitch_cv density_cv assign_cv freeze_gate seed_gate grain_out seed_uses_gate"
PLUGINS = {
    DRIZZLE: {
        "enum": "Port", "file": "drizzle.ttl", "name": "Drizzle", "count": 42, "hosts": "drizzle-defaults",
        "class": LV2.DelayPlugin,
        "permanent": f"""in_l in_r out_l out_r {GRAIN} feedback dry_wet reverb freeze seed seed_lock quality mono
gain assign_to gate_mode output {CVS} auto_gain enabled latency""".split(),
        "assign": {0: "Feedback", 1: "Dry/wet", 2: "Reverb", 3: "All"},
    },
    SYNTH: {
        "enum": "SynthPort", "file": "drizzle_synth.ttl", "name": "Drizzle Synth", "count": 36,
        "hosts": "synth-defaults", "class": LV2.GeneratorPlugin,
        "permanent": f"""out_l out_r {GRAIN} wavetable osc_grains reverb freeze seed seed_lock quality assign_to
gate_mode output {CVS} latency""".split(),
        "assign": {0: "Wavetable", 1: "Osc/grains", 2: "Reverb", 3: "All"},
    },
}
FORBIDDEN = re.compile(r"mutable|beads|gillet|emilie", re.I)
TOGGLES = {"freeze", "seed", "seed_lock", "mono", "auto_gain", "enabled", "seed_uses_gate",
           "time_uses_cv", "size_uses_cv", "shape_uses_cv", "pitch_uses_cv"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def declared_ports(enum):
    header = (ROOT / "src/ports.hpp").read_text(encoding="utf-8")
    body = re.search(rf"enum class {enum} : uint32_t \{{(.*?)\}};", header, re.S)
    require(body is not None, f"missing {enum} enum")
    declared = re.findall(r"(\w+)\s*=\s*(\d+),\s*//\s*(audio_in|audio_out|control_in|control_out|cv_in|cv_out)", body[1])
    return {int(index): (symbol, kind) for symbol, index, kind in declared}, body[1], len(declared)


def check_plugin(bundle, uri, spec):
    expected, body, declared = declared_ports(spec["enum"])
    count = spec["count"]
    require(declared == len(expected) == count, f"{spec['enum']}: count/duplicate index")
    require(set(expected) == set(range(count)), f"{spec['enum']}: non-contiguous indices")
    require(re.search(r"count\s*=\s*" + str(count) + r"\b", body), f"{spec['enum']}: count sentinel")
    require([expected[i][0] for i in range(len(spec["permanent"]))] == spec["permanent"],
            f"{spec['enum']}: permanent indices/symbols changed")

    graph = rdflib.Graph().parse(bundle / spec["file"], format="turtle")
    require((uri, RDF.type, LV2.Plugin) in graph and (uri, RDF.type, spec["class"]) in graph, f"{uri}: plugin class")
    # Peepo withholds an InstrumentPlugin as "played by MIDI notes"; Drizzle has no MIDI input.
    require((uri, RDF.type, LV2.InstrumentPlugin) not in graph, f"{uri}: not an instrument")
    require(graph.value(uri, DOAP.license) == URIRef("https://spdx.org/licenses/MIT"), "license")
    require(str(graph.value(uri, DOAP.name)) == spec["name"], "name")
    require(not FORBIDDEN.search(str(graph.value(uri, RDFS.comment))), "description names the original")
    require(not set(graph.objects(uri, LV2.requiredFeature)), "no required features")
    require(LV2.hardRTCapable in set(graph.objects(uri, LV2.optionalFeature)), "hardRTCapable")
    require(graph.value(uri, LV2.minorVersion) is not None and graph.value(uri, LV2.microVersion) is not None, "version")

    actual, symbols, defaults = {}, set(), {}
    for port in graph.objects(uri, LV2.port):
        index, symbol, name = int(graph.value(port, LV2["index"])), str(graph.value(port, LV2.symbol)), str(graph.value(port, LV2.name))
        require(index not in actual and symbol not in symbols, f"duplicate port {symbol}")
        symbols.add(symbol)
        require(re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", symbol), f"invalid symbol: {symbol}")
        require(name.isascii() and name.isprintable() and 0 < len(name) <= 16, f"{symbol}: name must be ASCII, <=16 characters")
        require(not FORBIDDEN.search(name), f"{symbol}: name names the original")
        types = set(graph.objects(port, RDF.type))
        directions = types & {LV2.InputPort, LV2.OutputPort}
        kinds = types & {LV2.AudioPort, LV2.ControlPort, LV2.CVPort}
        require(len(directions) == len(kinds) == 1, f"{symbol}: ambiguous port kind")
        kind = {LV2.AudioPort: "audio", LV2.ControlPort: "control", LV2.CVPort: "cv"}[next(iter(kinds))]
        kind += "_in" if LV2.InputPort in types else "_out"
        actual[index] = (symbol, kind)
        lo, hi = graph.value(port, LV2.minimum), graph.value(port, LV2.maximum)
        if LV2.ControlPort in types or LV2.CVPort in types:
            require(lo is not None and hi is not None and float(lo) <= float(hi), f"{symbol}: bounds")
            if LV2.InputPort in types:
                default = graph.value(port, LV2.default)
                require(default is not None and float(lo) <= float(default) <= float(hi), f"{symbol}: default")
                if LV2.ControlPort in types:
                    defaults[symbol] = float(default)
        props = set(graph.objects(port, LV2.portProperty))
        if LV2.enumeration in props:
            points = list(graph.objects(port, LV2.scalePoint))
            values = sorted(float(graph.value(p, RDF.value)) for p in points)
            require(LV2.integer in props and values == list(range(len(points))), f"{symbol}: enum values")
            require(values[0] == float(lo) and values[-1] == float(hi), f"{symbol}: enum bounds")
            require(all(str(graph.value(p, RDFS.label)).isascii() for p in points), f"{symbol}: labels")
        labels = {int(float(graph.value(p, RDF.value))): str(graph.value(p, RDFS.label)) for p in graph.objects(port, LV2.scalePoint)}
        if symbol == "quality":
            require(labels == QUALITY, "quality labels follow Processor::Media")
        if symbol == "assign_to":
            require(labels == spec["assign"], "assign_to labels follow Ui::MixParameter")
        if symbol in TOGGLES:
            require(LV2.toggled in props, f"{symbol}: toggled")
        if symbol == "enabled":
            require(graph.value(port, LV2.designation) == LV2.enabled, "enabled: designation")
        if symbol == "pitch":
            require(LV2.integer in props and (float(lo), float(hi)) == (-24, 24), "pitch: whole semitones, 2 octaves")
        if symbol == "latency":
            require(LV2.reportsLatency in props and graph.value(port, LV2.designation) == LV2.latency, "latency port")
        if symbol in ("output", "gain"):
            require(graph.value(port, UNITS.unit) == UNITS.db, f"{symbol}: dB")
        if kind == "cv_in":
            require((float(lo), float(hi)) == (-1, 1), f"{symbol}: CV range is 1.0 = 5 V, +-5 V")
    require(actual == expected, f"{spec['enum']}: ports differ: {actual}")
    check_host_defaults(defaults, spec["hosts"])
    return defaults


def check_host_defaults(defaults, name):
    """The test host starts every control at its Turtle default."""
    host = (ROOT / "tests/host.cpp").read_text(encoding="utf-8")
    body = re.search(rf"// {name}-begin(.*?)// {name}-end", host, re.S)
    require(body is not None, f"defaults: host.cpp {name} block")
    values = dict.fromkeys(defaults, 0.0)
    values.update({s: float(v) for s, v in re.findall(r"control\(\w+::(\w+)\) = (-?[0-9.]+)f;", body[1])})
    for symbol, value in defaults.items():
        require(abs(values[symbol] - value) < 1e-6,
                f"defaults: the test host gives {symbol} {values[symbol]}, the Turtle says {value}")


def check_fallbacks(defaults):
    """The C++ fallbacks for an unconnected control agree with the Turtle."""
    controls = (ROOT / "src/controls.cpp").read_text(encoding="utf-8")
    glue = (ROOT / "src/drizzle.cpp").read_text(encoding="utf-8")
    fallbacks = {s: float(v) for s, v in re.findall(r"\{Port::(\w+),\s*\w+,\s*(-?[0-9.]+)f,", controls)}
    fallbacks.update({s: float(b == "true") for s, b in re.findall(r"toggle\(ports, Port::(\w+), (true|false)\)", controls)})
    fallbacks.update({s: float(v) for s, v in re.findall(r"control\(Port::(\w+), (-?[0-9.]+),", glue)})
    require(len(fallbacks) >= 15, "defaults: C++ fallbacks not found")
    for symbol, value in fallbacks.items():
        require(symbol in defaults and abs(defaults[symbol] - value) < 1e-6,
                f"defaults: {symbol} falls back to {value}, the Turtle says {defaults.get(symbol)}")


def check(bundle):
    manifest = rdflib.Graph().parse(bundle / "manifest.ttl", format="turtle")
    require(set(manifest.subjects(RDF.type, LV2.Plugin)) == set(PLUGINS), "manifest: two plugins")
    for uri, spec in PLUGINS.items():
        require(manifest.value(uri, LV2.binary) == URIRef((bundle / "drizzle.so").as_uri()), "manifest: binary")
        require(manifest.value(uri, RDFS.seeAlso) == URIRef((bundle / spec["file"]).as_uri()), "manifest: seeAlso")
    defaults = check_plugin(bundle, DRIZZLE, PLUGINS[DRIZZLE])
    check_fallbacks(defaults)
    check_plugin(bundle, SYNTH, PLUGINS[SYNTH])
    print(f"PASS ttl_check: {PLUGINS[DRIZZLE]['count']} + {PLUGINS[SYNTH]['count']} ports match ports.hpp; "
          "names, bounds, defaults, labels, license and bundle")


if __name__ == "__main__":
    try:
        check(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "drizzle.lv2").resolve())
    except Exception as error:
        sys.exit(f"FAIL ttl_check: {error}")
