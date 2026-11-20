#include "SceneAudioPicker.h"

#include <set>

namespace sg::monitor {

namespace {

void walk(const SceneNode &scene, const std::string &id, int depth, std::set<const SceneNode *> &seen, PickResult &r)
{
    if (depth > 8 || !seen.insert(&scene).second)
        return;
    for (const auto &it : scene.items) {
        if (!it.visible)
            continue;
        if (it.sourceId == id) {
            if (r.sourceName.empty())
                r.sourceName = it.sourceName;
            ++r.matches;
        } else if (it.child) {
            walk(*it.child, id, depth + 1, seen, r);
        }
    }
}

// El último recuadro de escena es la esquina inferior derecha salvo en estos dos diseños.
bool lastCellIsBottomRight(int layout)
{
    return layout != 1 && layout != 3;
}

} // namespace

PickResult pickSoundGridSource(const SceneNode &scene, const std::string &sourceId)
{
    PickResult r;
    std::set<const SceneNode *> seen;
    walk(scene, sourceId, 0, seen, r);
    return r;
}

int multiviewSceneSlots(int layout)
{
    switch (layout) {
    case 4: return 24;
    case 5: return 18;
    case 6: return 4;
    case 7: return 9;
    case 8: return 16;
    case 9: return 25;
    default: return 8; // 0–3: diseños de 8 escenas (y valores desconocidos, como OBS)
    }
}

const char *multiviewLayoutName(int layout)
{
    switch (layout) {
    case 1: return "Horizontal, abajo (8 escenas)";
    case 2: return "Vertical, izquierda (8 escenas)";
    case 3: return "Vertical, derecha (8 escenas)";
    case 4: return "Horizontal, arriba (24 escenas)";
    case 5: return "Horizontal, arriba (18 escenas)";
    case 6: return "Solo escenas (4 escenas)";
    case 7: return "Solo escenas (9 escenas)";
    case 8: return "Solo escenas (16 escenas)";
    case 9: return "Solo escenas (25 escenas)";
    default: return "Horizontal, arriba (8 escenas)";
    }
}

MultiviewPlan planMonitorPlacement(const std::vector<SceneEntry> &scenes, int layout)
{
    MultiviewPlan p;
    p.slots = multiviewSceneSlots(layout);

    int rank = 0;
    for (const auto &s : scenes) {
        if (!s.showInMultiview)
            continue;
        ++rank;
        if (s.isMonitor && rank <= p.slots)
            p.currentCell = rank;
    }

    // Lista sin el monitor: posiciones (0 = primera) de las escenas visibles en el Multiview.
    std::vector<int> visibleIdx;
    std::vector<std::string> visibleNames;
    int idx = 0;
    for (const auto &s : scenes) {
        if (s.isMonitor)
            continue;
        if (s.showInMultiview) {
            visibleIdx.push_back(idx);
            visibleNames.push_back(s.name);
        }
        ++idx;
    }
    const int before = p.slots - 1; // escenas que deben ir delante del monitor
    int cellIfPlaced;
    if (static_cast<int>(visibleIdx.size()) >= before) {
        p.targetPosition = visibleIdx[static_cast<std::size_t>(before - 1)] + 2;
        if (static_cast<int>(visibleNames.size()) > before)
            p.displaced = visibleNames[static_cast<std::size_t>(before)];
        cellIfPlaced = p.slots;
    } else {
        p.enoughScenes = false;
        p.targetPosition = visibleIdx.empty() ? 1 : visibleIdx.back() + 2;
        cellIfPlaced = static_cast<int>(visibleIdx.size()) + 1;
    }
    p.alreadyPlaced = p.currentCell != 0 && p.currentCell == cellIfPlaced;
    return p;
}

std::string describePlan(const MultiviewPlan &p, int layout, const std::string &monitorScene)
{
    std::string s = "Multiview: " + std::string(multiviewLayoutName(layout)) + ", " + std::to_string(p.slots) +
                    " recuadros de escena.\n";
    if (!lastCellIsBottomRight(layout))
        s += "Aviso: en este diseño el último recuadro de escena no es la esquina inferior derecha.\n";
    if (p.alreadyPlaced && p.enoughScenes) {
        s += "La escena \"" + monitorScene + "\" ya ocupa el último recuadro del Multiview.";
        return s;
    }
    if (p.alreadyPlaced) {
        s += "La escena \"" + monitorScene + "\" ya está justo después de la última escena visible (recuadro " +
             std::to_string(p.currentCell) + ").";
    } else {
        s += "Arrastra la escena \"" + monitorScene + "\" a la posición " + std::to_string(p.targetPosition) +
             " de la lista de escenas (contando desde arriba).";
    }
    if (!p.enoughScenes)
        s += "\nHay menos escenas que recuadros: el monitor quedará en el primer recuadro libre, no en la "
             "esquina. Elige un diseño de Multiview con menos recuadros o añade escenas.";
    else if (!p.displaced.empty() && !p.alreadyPlaced)
        s += "\nLa escena \"" + p.displaced + "\" dejará de verse en el Multiview (ocupaba ese recuadro).";
    return s;
}

} // namespace sg::monitor
