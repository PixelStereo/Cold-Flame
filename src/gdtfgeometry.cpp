#include "gdtf.h"
#include <functional>

namespace Flame {
namespace {
struct Geometry {
    QString name, reference;
    QList<int> children;
    bool isReference = false;
    QList<QPair<int, int>> breaks; // Output break, one-based offset.
};
struct RawChannel { FixtureParameter parameter; QString geometry, dmxBreak; };
struct RawMode { FixtureMode mode; QString root; QList<RawChannel> channels; };
struct Instance { QString cell; int shift = 0; bool referenced = false, explicitBreak = false; };

FixtureMode expand(const RawMode &raw, const QList<Geometry> &geometry, const QList<int> &roots) {
    FixtureMode mode = raw.mode;
    auto reject = [&](QString issue) { mode.parameters.clear(); mode.footprint = 0; mode.unsupported = issue; return mode; };
    auto namedRoots = [&](const QString &name) {
        QList<int> matches; for (int index : roots) if (geometry[index].name == name) matches << index; return matches;
    };
    QSet<int> topLevel(roots.begin(), roots.end());
    QMap<QString, QList<Instance>> instances;
    QSet<int> active;
    QString issue;
    int visits = 0;
    bool hasReferences = false;
    QSet<QString> referencePaths;
    std::function<bool(int, QStringList, int, bool, bool, int)> visit;
    visit = [&](int index, QStringList path, int shift, bool referenced, bool explicitBreak, int depth) {
        if (depth > 64 || ++visits > 4096) { issue = "Geometry expansion exceeds the depth or instance limit"; return false; }
        if (path.join('/').size() > 2048) { issue = "Expanded cell path exceeds 2048 characters"; return false; }
        if (active.contains(index)) { issue = "Cyclic geometry reference"; return false; }
        const auto &node = geometry[index];
        active.insert(index);
        if (node.isReference) {
            hasReferences = true;
            const auto targets = namedRoots(node.reference);
            if (targets.size() != 1 || !topLevel.contains(targets.first())) { issue = "Missing, ambiguous or non-root referenced geometry: " + node.reference; return false; }
            int offset = 1; bool mapped = false;
            for (const auto &mapping : node.breaks) if (mapping.first == 1) {
                if (mapped) { issue = "Duplicate geometry-reference Break mapping"; return false; }
                offset = mapping.second; mapped = true;
            }
            if (offset < 1 || offset > 512 || shift + offset - 1 > 511) { issue = "Invalid geometry-reference DMX offset"; return false; }
            path << node.name;
            const QString cellPath = path.join('/');
            if (referencePaths.contains(cellPath)) { issue = "Duplicate geometry-reference cell path"; return false; }
            referencePaths.insert(cellPath);
            if (!visit(targets.first(), path, shift + offset - 1, true, mapped, depth + 1)) return false;
        } else {
            // A referenced source root has the instance's path, not its template name.
            instances[node.name] << Instance{path.join('/'), shift, referenced, explicitBreak};
            for (int child : node.children) {
                auto childPath = path;
                if (!geometry[child].isReference) childPath << geometry[child].name;
                if (!visit(child, childPath, shift, referenced, explicitBreak, depth + 1)) return false;
            }
        }
        active.remove(index); return true;
    };
    if (!raw.root.isEmpty()) {
        const auto root = namedRoots(raw.root);
        if (root.size() != 1 || !topLevel.contains(root.first())) return reject("Missing or ambiguous mode geometry: " + raw.root);
        if (!visit(root.first(), {}, 0, false, false, 0)) return reject(issue);
    } else if (!geometry.isEmpty()) {
        bool anyReferences = false; for (const auto &node : geometry) anyReferences |= node.isReference;
        if (anyReferences) return reject("A reference-based mode must specify its root geometry");
    }
    QSet<QString> dimmerGeometry;
    QMap<QString, QSet<QString>> emitterGeometry;
    for (const auto &channel : raw.channels) {
        if (channel.parameter.intensity && !channel.geometry.isEmpty()) dimmerGeometry.insert(channel.geometry);
        if (channel.parameter.attribute.startsWith("ColorAdd_") && !channel.geometry.isEmpty())
            emitterGeometry[channel.parameter.attribute].insert(channel.geometry);
    }
    bool directCells = dimmerGeometry.size() > 1;
    for (const auto &owners : emitterGeometry) directCells |= owners.size() > 1;
    // Preserve legacy XML parameter ordering; each repeated source expands in instance order.
    for (const auto &channel : raw.channels) {
        QList<Instance> occurrences;
        if (raw.root.isEmpty() || channel.geometry.isEmpty()) {
            if (hasReferences && channel.geometry.isEmpty()) return reject("A reference-based DMX channel must specify its geometry");
            occurrences << Instance{};
        } else occurrences = instances.value(channel.geometry);
        if (occurrences.isEmpty()) return reject("DMX channel geometry is not reachable from the mode root: " + channel.geometry);
        for (const auto &instance : occurrences) {
            if (channel.dmxBreak == "Overwrite") {
                if (!instance.referenced || !instance.explicitBreak) return reject("Overwrite requires a geometry-reference Break mapping");
            } else if (channel.dmxBreak != "1") return reject("Multiple source DMX breaks are not supported");
            auto parameter = channel.parameter; parameter.cell = hasReferences || directCells ? instance.cell : QString();
            for (int &offset : parameter.offsets) { offset += instance.shift; mode.footprint = qMax(mode.footprint, offset); }
            mode.parameters << parameter;
            if (mode.parameters.size() > 512) return reject("Expanded mode exceeds 512 parameter controls");
        }
    }
    ensureCellControls(mode);
    if (!validateMode(mode, issue) && mode.unsupported.isEmpty()) mode.unsupported = issue;
    return mode;
}
}

void ensureCellControls(FixtureMode &mode) {
    QStringList cells; QSet<QString> dimmers, dimmable;
    for (const auto &parameter : mode.parameters) if (!parameter.cell.isEmpty()) {
        if (!cells.contains(parameter.cell)) cells << parameter.cell;
        if (parameter.intensity) dimmers.insert(parameter.cell);
        if (parameter.intensity || parameter.attribute.startsWith("ColorAdd_")) {
            auto path = parameter.cell.split('/');
            while (!path.isEmpty()) { dimmable.insert(path.join('/')); path.removeLast(); }
        }
    }
    for (const auto &cell : cells) if (!dimmers.contains(cell) && dimmable.contains(cell)) mode.parameters << FixtureParameter{"Dimmer", {}, 100, true, false, cell};
}

bool parseGdtfDescription(const QByteArray &xml, FixtureProfile &result, QString &error) {
    QXmlStreamReader reader(xml);
    FixtureProfile profile;
    QList<Geometry> geometry; QList<int> roots;
    QList<QPair<int, int>> stack; // XML depth, geometry index.
    QList<RawMode> modes; RawMode mode; RawChannel channel;
    bool inGeometries = false, inMode = false, inChannel = false;
    int depth = 0, nodes = 0;
    QString defaultValue, initialFunction;
    auto fail = [&](QString issue) { error = issue; return false; };
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isDTD()) return fail("DTD is not allowed");
        if (reader.isStartElement()) {
            if (++depth > 128 || ++nodes > 100000) return fail("GDTF description exceeds parsing limits");
            const auto name = reader.name(); const auto attributes = reader.attributes();
            if (name == "Geometries") inGeometries = true;
            else if (inGeometries) {
                if (name == "Break" && !stack.isEmpty() && geometry[stack.last().second].isReference) {
                    bool okBreak = true, okOffset = true;
                    int output = attributes.hasAttribute("DMXBreak") ? attributes.value("DMXBreak").toInt(&okBreak) : 1;
                    const QString offset = attributes.value("DMXOffset").toString(); int relative = 1;
                    if (!offset.isEmpty()) {
                        if (offset.contains('.')) { const auto parts = offset.split('.'); relative = parts.size() == 2 && parts[0] == "1" ? parts[1].toInt(&okOffset) : 0; }
                        else relative = offset.toInt(&okOffset);
                    }
                    geometry[stack.last().second].breaks << qMakePair(okBreak ? output : 0, okOffset ? relative : 0);
                } else if (!attributes.value("Name").isEmpty()) {
                    Geometry node; node.name = attributes.value("Name").toString(); node.isReference = name == "GeometryReference";
                    node.reference = attributes.value("Geometry").toString(); const int index = geometry.size(); geometry << node;
                    if (node.name.size() > 256 || node.reference.size() > 256) return fail("Geometry names exceed 256 characters");
                    if (stack.isEmpty()) roots << index; else geometry[stack.last().second].children << index;
                    stack << qMakePair(depth, index);
                }
            } else if (name == "FixtureType") {
                profile.name = attributes.value("Name").toString(); profile.manufacturer = attributes.value("Manufacturer").toString();
            } else if (name == "DMXMode") {
                mode = {}; mode.mode.name = attributes.value("Name").toString(); mode.root = attributes.value("Geometry").toString(); inMode = true;
            } else if (inMode && name == "DMXChannel") {
                channel = {}; inChannel = true; defaultValue = attributes.value("Default").toString(); initialFunction = attributes.value("InitialFunction").toString();
                channel.geometry = attributes.value("Geometry").toString(); channel.dmxBreak = attributes.value("DMXBreak").toString(); if (channel.dmxBreak.isEmpty()) channel.dmxBreak = "1";
                const QString offsets = attributes.value("Offset").toString();
                if (!offsets.isEmpty() && offsets != "None") for (const auto &value : offsets.split(',')) {
                    bool ok; int offset = value.toInt(&ok);
                    if (!ok || offset < 1 || offset > 512) mode.mode.unsupported = "Invalid or virtual DMX channel offset";
                    else channel.parameter.offsets << offset;
                }
            } else if (inChannel && name == "LogicalChannel") {
                if (!channel.parameter.attribute.isEmpty()) mode.mode.unsupported = "Multiplexed logical channels are not supported";
                channel.parameter.attribute = attributes.value("Attribute").toString(); channel.parameter.intensity = channel.parameter.attribute == "Dimmer";
                channel.parameter.snap = attributes.value("Snap") == "Yes" || attributes.value("Snap") == "On";
            } else if (inMode && name == "Relation") {
                const QString master = attributes.value("Master").toString();
                const QString follower = attributes.value("Follower").toString().section('.', 0, 0);
                bool supported = false;
                for (const auto &candidate : mode.channels) if (candidate.parameter.intensity && candidate.parameter.offsets.isEmpty() &&
                    master == candidate.geometry + "_Dimmer")
                    for (const auto &target : mode.channels) if (target.geometry == candidate.geometry &&
                        target.parameter.attribute.startsWith("ColorAdd_") && follower == target.geometry + "_" + target.parameter.attribute)
                        supported = attributes.value("Type") == "Multiply";
                if (!supported) mode.mode.unsupported = "This channel relation is not supported; only additive color multiplied by a virtual dimmer is supported";
            } else if (inMode && name == "ModeMaster") mode.mode.unsupported = "Mode masters are not supported";
            else if (inChannel && name == "ChannelFunction") {
                if (!attributes.value("ModeMaster").isEmpty()) mode.mode.unsupported = "Mode-dependent channels are not supported";
                if (defaultValue.isEmpty() || (!initialFunction.isEmpty() && initialFunction.section('.', -1) == attributes.value("Name"))) defaultValue = attributes.value("Default").toString();
            }
        } else if (reader.isEndElement()) {
            if (reader.name() == "DMXChannel" && inChannel) {
                inChannel = false;
                if (!defaultValue.isEmpty()) {
                    const auto parts = defaultValue.split('/'); bool valueOk, widthOk;
                    const quint64 value = parts.value(0).toULongLong(&valueOk); const int width = parts.value(1).toInt(&widthOk);
                    if (parts.size() != 2 || !valueOk || !widthOk || width < 1 || width > 4 || value > ((quint64(1) << (8 * width)) - 1)) mode.mode.unsupported = "Invalid DMX default";
                    else channel.parameter.home = double(value) * 100. / double((quint64(1) << (8 * width)) - 1);
                }
                mode.channels << channel;
                if (mode.channels.size() > 512) return fail("Source mode exceeds 512 DMX channel controls");
            } else if (reader.name() == "DMXMode" && inMode) {
                if (modes.size() >= 256) return fail("Fixture exceeds 256 DMX modes");
                modes << mode; inMode = false;
            }
            if (!stack.isEmpty() && stack.last().first == depth) stack.removeLast();
            if (reader.name() == "Geometries") inGeometries = false;
            --depth;
        }
    }
    if (reader.hasError() || profile.name.isEmpty() || modes.isEmpty()) return fail("Invalid GDTF description or missing DMX mode");
    if (profile.name.size() > 512 || profile.manufacturer.size() > 512) return fail("Fixture metadata exceeds supported lengths");
    for (const auto &raw : modes) { auto expanded = expand(raw, geometry, roots); for (const auto &parameter : expanded.parameters) if (!expanded.attributes.contains(parameter.attribute)) expanded.attributes << parameter.attribute; profile.modes << expanded; }
    result = profile; return true;
}
}
