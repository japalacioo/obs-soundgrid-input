#pragma once
// Lógica (sin libobs) para elegir qué fuente SoundGrid muestra el monitor y dónde colocar la escena
// del monitor para que ocupe el último recuadro del Multiview. La capa OBS (AudioMonitorSource.cpp)
// traduce escenas y ajustes de OBS a estas estructuras.
#include <memory>
#include <string>
#include <vector>

namespace sg::monitor {

constexpr const char *kSoundGridSourceId = "soundgrid_input";

// Árbol de una escena: elementos en el orden de la lista de OBS (arriba primero).
struct SceneNode;
struct SceneItemNode {
    std::string sourceId;   // p. ej. "soundgrid_input", "scene", "group"
    std::string sourceName;
    bool visible = true;    // ojo de la lista de fuentes
    std::shared_ptr<SceneNode> child; // escena anidada o grupo
};
struct SceneNode {
    std::string name;
    std::vector<SceneItemNode> items;
};

struct PickResult {
    std::string sourceName; // vacío: la escena no contiene una fuente SoundGrid visible
    int matches = 0;        // fuentes SoundGrid visibles encontradas (se muestra la primera)
};
// Primera fuente SoundGrid visible recorriendo la escena de arriba abajo, entrando en grupos y
// escenas anidadas visibles (profundidad máxima 8, sin ciclos).
PickResult pickSoundGridSource(const SceneNode &scene, const std::string &sourceId = kSoundGridSourceId);

// --------------------------------------------------------------- Multiview

// Recuadros de escena de cada diseño de Multiview de OBS (frontend/components/Multiview.hpp).
int multiviewSceneSlots(int layout);
const char *multiviewLayoutName(int layout);

struct SceneEntry {
    std::string name;
    bool showInMultiview = true; // ajuste privado "show_in_multiview" de la escena
    bool isMonitor = false;
};

struct MultiviewPlan {
    int slots = 8;            // recuadros de escena del diseño
    int currentCell = 0;      // recuadro donde está ahora el monitor (1 = primero; 0 = no se ve)
    int targetPosition = 0;   // posición (1 = primera) en la lista de escenas para el último recuadro
    bool alreadyPlaced = false;
    bool enoughScenes = true; // hay escenas para llenar los recuadros anteriores
    std::string displaced;    // escena que hoy ocupa el último recuadro y dejaría de verse
};
// Calcula dónde debe estar la escena del monitor en la lista de escenas para que el Multiview la
// dibuje en el último recuadro (abajo a la derecha). OBS no tiene API para reordenar escenas: el
// operador la arrastra a targetPosition.
MultiviewPlan planMonitorPlacement(const std::vector<SceneEntry> &scenes, int layout);
std::string describePlan(const MultiviewPlan &p, int layout, const std::string &monitorScene);

} // namespace sg::monitor
