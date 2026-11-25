#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "mods/svc/save.h"

#include "d/actor/d_a_player.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"

#include <cmath>
#include <cstdint>
#include <cstring>

DEFINE_MOD();

IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(SaveService, svc_save);

namespace {

constexpr char kLocationBlobName[] = "save-location";
constexpr uint32_t kLocationMagic = 0x4C4F4352; // LOCR
constexpr uint16_t kLocationVersion = 1;
constexpr size_t kStageNameCapacity = 16;
constexpr uint32_t kSaveSlotCount = 3;

struct LocationRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    char stage[kStageNameCapacity];
    float position[3];
    int16_t angleY;
    int8_t room;
    int8_t layer;
};

struct PendingSave {
    LocationRecord location{};
    uint32_t slot = 0;
    bool valid = false;
};

struct LoadedLocation {
    LocationRecord location{};
    bool valid = false;
};

static_assert(sizeof(LocationRecord) == 40);

SaveObserverHandle g_saveObserver = 0;
uint32_t g_activeSlot = 0;
bool g_hasActiveSlot = false;
PendingSave g_pendingSave;
LoadedLocation g_loadedLocation;

bool valid_location(const LocationRecord& location) {
    return location.magic == kLocationMagic && location.version == kLocationVersion &&
           location.size == sizeof(LocationRecord) && location.stage[0] != '\0' &&
           std::memchr(location.stage, '\0', sizeof(location.stage)) != nullptr &&
           std::isfinite(location.position[0]) && std::isfinite(location.position[1]) &&
           std::isfinite(location.position[2]) && location.room >= 0 && location.room < 64 &&
           location.layer >= -1 && location.layer < 15;
}

void on_new_save(ModContext*, uint32_t slot, void*) {
    g_activeSlot = slot;
    g_hasActiveSlot = true;
    g_pendingSave = {};
    g_loadedLocation = {};
}

void on_save_loaded(ModContext*, uint32_t slot, void*) {
    g_activeSlot = slot;
    g_hasActiveSlot = true;
    g_pendingSave = {};
    g_loadedLocation = {};

    LocationRecord location{};
    size_t size = sizeof(location);
    const ModResult result = svc_save->get_blob(mod_ctx, kLocationBlobName, &location, &size);
    if (result == MOD_UNAVAILABLE) {
        mods::log::info("slot {} has no save-location metadata; using the vanilla checkpoint", slot);
        return;
    }
    if (result != MOD_OK || size != sizeof(location) || !valid_location(location)) {
        mods::log::error("save-location metadata is invalid; using the vanilla checkpoint");
        return;
    }

    g_loadedLocation.location = location;
    g_loadedLocation.valid = true;
    mods::log::info("loaded save location for slot {}: {} room {}", slot, location.stage,
        static_cast<int>(location.room));
}

void on_save_written(ModContext*, uint32_t slot, void*) {
    if (g_pendingSave.valid && slot == g_pendingSave.slot) {
        // SaveService flushes sidecars after this callback, so failed card writes never persist it.
        const ModResult result =
            svc_save->set_blob(mod_ctx, kLocationBlobName, &g_pendingSave.location,
                sizeof(g_pendingSave.location));
        if (result != MOD_OK) {
            mods::log::error("failed to store save-location metadata (result {})",
                static_cast<int>(result));
            const ModResult clearResult = svc_save->delete_blob(mod_ctx, kLocationBlobName);
            if (clearResult != MOD_OK && clearResult != MOD_INVALID_ARGUMENT) {
                mods::log::error("failed to clear stale save-location metadata (result {})",
                    static_cast<int>(clearResult));
            }
        } else {
            mods::log::info("stored save location for slot {}: {} room {}", slot,
                g_pendingSave.location.stage, static_cast<int>(g_pendingSave.location.room));
        }
    } else if (slot == g_activeSlot) {
        const ModResult result = svc_save->delete_blob(mod_ctx, kLocationBlobName);
        if (result != MOD_OK && result != MOD_INVALID_ARGUMENT) {
            mods::log::error("failed to clear stale save-location metadata (result {})",
                static_cast<int>(result));
        }
    }
    g_pendingSave = {};
}

bool capture_location(LocationRecord& location) {
    daPy_py_c* player = daPy_getLinkPlayerActorClass();
    const char* stage = dComIfGp_getStartStageName();
    if (player == nullptr || stage == nullptr || stage[0] == '\0') {
        return false;
    }

    const size_t stageLength = std::strlen(stage);
    if (stageLength >= sizeof(location.stage)) {
        return false;
    }

    location = {};
    location.magic = kLocationMagic;
    location.version = kLocationVersion;
    location.size = static_cast<uint16_t>(sizeof(location));
    std::memcpy(location.stage, stage, stageLength + 1);
    location.position[0] = player->current.pos.x;
    location.position[1] = player->current.pos.y;
    location.position[2] = player->current.pos.z;
    location.angleY = player->shape_angle.y;
    location.room = fopAcM_GetRoomNo(player);
    location.layer = dComIfGp_getStartStageLayer();
    return valid_location(location);
}

} // namespace

DEFINE_HOOK_SYMBOL(
    "dComIfGs_setMemoryToCard", void(u8*, int), SaveMemoryToCard);
DEFINE_HOOK_SYMBOL(
    "dComIfGp_setStartStage", void(dStage_startStage_c*), StartStage);

static HookAction on_memory_to_card_pre(ModContext*, void* args, void*, void*) {
    g_pendingSave = {};
    const int slot = mods::arg<int>(args, 1);
    if (!g_hasActiveSlot || slot < 0 || slot >= static_cast<int>(kSaveSlotCount) ||
        static_cast<uint32_t>(slot) != g_activeSlot)
    {
        return HOOK_CONTINUE;
    }

    LocationRecord location{};
    if (capture_location(location)) {
        g_pendingSave.location = location;
        g_pendingSave.slot = static_cast<uint32_t>(slot);
        g_pendingSave.valid = true;
        mods::log::info("captured save location for slot {}: {} room {}", slot, location.stage,
            static_cast<int>(location.room));
    } else {
        mods::log::error("could not capture Link's location before the save write");
    }
    return HOOK_CONTINUE;
}

static HookAction on_start_stage_pre(ModContext*, void* args, void*, void*) {
    if (!g_loadedLocation.valid) {
        return HOOK_CONTINUE;
    }

    const LocationRecord& location = g_loadedLocation.location;
    dStage_startStage_c* stage = mods::arg<dStage_startStage_c*>(args, 0);
    if (stage == nullptr) {
        mods::log::error("could not override the loaded save's start stage");
        return HOOK_CONTINUE;
    }

    stage->set(location.stage, location.room, -1, location.layer);
    mods::log::info("overriding start stage with save location {} room {}", location.stage,
        static_cast<int>(location.room));
    return HOOK_CONTINUE;
}

static void on_start_stage_post(ModContext*, void*, void*, void*) {
    if (!g_loadedLocation.valid) {
        return;
    }

    const LocationRecord& location = g_loadedLocation.location;
    const cXyz position{location.position[0], location.position[1], location.position[2]};
    dComIfGp_getStartStage()->set(location.stage, location.room, -1, location.layer);
    dComIfGs_setRestartRoom(position, location.angleY, location.room);
    // Point -1 uses these parameters for Link; preserve the no-start-event sentinel.
    dComIfGs_setRestartRoomParam(
        0xFF000000u | (static_cast<uint32_t>(location.room) & 0x3F));
    mods::log::info("restoring Link to {} room {} at ({}, {}, {})", location.stage,
        static_cast<int>(location.room), location.position[0], location.position[1],
        location.position[2]);
    g_loadedLocation = {};
}

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    ModResult result = mods::hook::add_pre<SaveMemoryToCard>(on_memory_to_card_pre);
    if (result != MOD_OK) {
        mods::log::error("failed to install save-memory hook (result {})", static_cast<int>(result));
        return result;
    }

    result = mods::hook::add_pre<StartStage>(on_start_stage_pre);
    if (result != MOD_OK) {
        mods::log::error("failed to install start-stage pre-hook (result {})",
            static_cast<int>(result));
        return result;
    }

    result = mods::hook::add_post<StartStage>(on_start_stage_post);
    if (result != MOD_OK) {
        mods::log::error("failed to install start-stage post-hook (result {})",
            static_cast<int>(result));
        return result;
    }

    result = svc_save->observe_saves(
        mod_ctx, on_new_save, on_save_loaded, on_save_written, nullptr, &g_saveObserver);
    if (result != MOD_OK) {
        mods::log::error("failed to register save observer (result {})", static_cast<int>(result));
        return result;
    }

    mods::log::info("save location restoration initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    if (g_saveObserver != 0) {
        const ModResult result = svc_save->unobserve_saves(mod_ctx, g_saveObserver);
        if (result != MOD_OK) {
            mods::log::error("failed to remove save observer (result {})", static_cast<int>(result));
            return result;
        }
        g_saveObserver = 0;
    }
    return MOD_OK;
}
}
