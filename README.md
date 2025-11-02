# 🎯 Know Your Limits

A realistic collision detection mod for Skyrim Special Edition that adds dynamic size compatibility checks during intimate animations.

## 📖 What This Mod Does

**Know Your Limits** introduces realistic physical limitations to intimate encounters in Skyrim. The mod monitors the positions of anatomical bones during animations and moves (translates) bones to prevent unrealistic interpenetration during intimate animations.

### ⚙️ Core Mechanics

The mod works by:

1. **🔍 Real-time Bone Monitoring**: Uses SKSE to track the 3D positions of specific bones during animations
2. **📐 Directional Penetration Detection**: Calculates how far anatomy has penetrated beyond realistic limits in the forward direction
3. **📏 Bone Translation**: Automatically translates middle bones when penetration exceeds configured thresholds to prevent interpenetration
4. **🔄 Automatic Restoration**: Restores original translations when animations end or when anatomy is no longer over-penetrating

### 🎬 Supported Animation Types

- **👄 Oral Actions**: Monitors mouth/head limitations for blowjob and deepthroat animations
- **🔸 Vaginal Interactions**: Tracks pelvic bone positioning for vaginal animations  
- **🔹 Anal Interactions**: Monitors spine positioning for anal animations

## 🔧 How the SKSE Plugin Works

The core functionality is implemented through a native SKSE plugin that provides three main functions:

### 📡 `RegisterBoneMonitor`
Starts monitoring specific bones between two actors:
- **Probe Actor**: The actor with anatomy to monitor (e.g., male)
- **Probe Bones**: Chain of bones representing the anatomy (e.g., penis bones from base to tip)
- **Target Actor**: The receiving actor (e.g., female)
- **Target Bone**: The bone representing the interaction point (e.g., head, pelvis)
- **Threshold**: Distance threshold for translation (negative values allow pre-emptive translation)
    - Note: Monitors run indefinitely until stopped via `StopBoneMonitor` (no duration parameter).

### 🛑 `StopBoneMonitor`
Stops monitoring for specified actors or all actors if none specified.

### ♻️ `ResetScaledBones`
Restores original bone translations for specified actors or all actors if none specified (this is a Papyrus-native function; confirm native implementation in the plugin if you rely on it at runtime).

### 🔬 Technical Details

- **📊 Penetration Calculation**: Uses directional vectors to determine how far anatomy extends beyond the target point
- **⛓️ Translation**: When the threshold is breached, all middle bones from the penetration point to the tip are translated
- **🔒 Thread Safety**: All operations are queued on the UI thread to prevent crashes
- **⚡ Performance Optimized**: Monitoring defaults to a 50ms interval (≈20 FPS) for a balance of responsiveness and performance

## 📝 Configuration

The mod uses JSON configuration files located in `SKSE/plugins/TT_KnowYourLimits/`:

### 🎛️ Main Configuration (`config.json`)

The plugin looks for a configuration file at `SKSE/plugins/KnowYourLimits/config.json`. This file controls the runtime behavior of the monitor and scaling system. Below are the fields the plugin recognizes, their types, typical defaults, and a short explanation.

Fields (present in config.json and used by Papyrus scripts):
- `general.intervalMs` (int, default: `50`) — Global monitor interval used by scripts; can be applied to the plugin via `SetTickInterval`.
- `penisBones` (array of strings) — Bones comprising the probe chain (e.g., base, multiple middle bones, tip) that will be translated when overlapping the target. Make sure bones names starting with `CME` are for changing positions, start and end bone are starting with `NPC` prefix.
- Per-action configuration objects such as `oral`, `vaginal`, `anal` with these members:
  - `threshold` (float) — Penetration threshold in game units. `0` - is when tip bone at same position as target bone. `negative` values allow pre-emptive translation. `positive` values require actual penetration before translation occurs. Should be larger than `restoreThreshold`.
  - `restoreThreshold` (float) — The threshold at or below which bones are restored. `0` - is when tip bone at same position as target bone. `negative` values allow pre-emptive restoration. `positive` values require actual penetration before restoration occurs. Should be less than `threshold`.
  - `ostimActions` (array of strings) — OStim action names used to identify scenes that should trigger monitoring.
  - `sexlabTags` (array of strings) — SexLab tags that can also be used to identify action types.
  - `bone` (string) — The target bone name on the receiving actor (e.g., `NPC Head [Head]`).

Notes:
- Distances are expressed in Skyrim's world units. If your body or animation framework uses skeleton scale modifiers, the effective distances will be scaled accordingly.
- If you want extremely responsive behavior (lower `general.intervalMs`), monitor for performance regressions.

Example `config.json` (actual file included in `SKSE/plugins/TT_KnowYourLimits/config.json`):

```json
{
    "general": { "intervalMs": 50 },
    "penisBones": [
        "NPC Genitals01 [Gen01]",
        "CME Genitals03 [Gen03]",
        "CME Genitals04 [Gen04]",
        "CME Genitals05 [Gen05]",
        "NPC Genitals06 [Gen06]"
    ],
    "oral": {
        "restoreThreshold": -8.0,
        "threshold": -3.0,
        "ostimActions": [
            "blowjob",
            "deepthroat"
        ],
        "sexlabTags": [],
        "bone": "NPC Head [Head]"
    },
    "vaginal": {
        "restoreThreshold": -2.0,
        "threshold": 4.5,
        "ostimActions": [
            "vaginalsex"
        ],
        "sexlabTags": [],
        "bone": "NPC Pelvis [Pelv]"
    },
    "anal": {
        "restoreThreshold": -1.0,
        "threshold": 3.0,
        "ostimActions": [
            "analsex"
        ],
        "sexlabTags": [],
        "bone": "NPC Spine [Spn0]"
    }
}
```

Tip: If you use a custom body mod, add or substitute bone names in `penisBones` to match your skeleton. Scripts (Papyrus) will use those names when creating monitors.

## 📦 Dependencies

### ✅ Required
- **SKSE64** (Skyrim Script Extender)
- **PapyrusUtil** - For data storage and manipulation

### 🔌 Supported Frameworks
- **OStim NG** - Primary integration for animation detection
- Other frameworks can be added through Papyrus scripts

## 💾 Installation

1. 📥 Install with your mod manager
2. ✔️ Ensure all dependencies are installed
3. 📚 Load after animation frameworks
4. ⚙️ Customize configuration files if desired

## 🤝 Compatibility

- ✅ **Safe to install mid-playthrough**
- ✅ **Safe to uninstall** (will restore all moved bones automatically)
- ✅ Compatible with most body mods and animation frameworks
- ⚠️ May conflict with other mods that manipulate bone translations or animation poses

## 🔧 Troubleshooting

**⚠️ Performance issues**:
- Monitoring defaults to a 50ms interval (~20 FPS) for performance; change `general.intervalMs` in `config.json` or call `KnowYourLimits.SetTickInterval` at runtime to tune the update frequency.
- Large numbers of simultaneous animations may cause brief lag

**🔄 Bones not restoring**:
- Try save and reload game to force restoration

## 🛠️ Advanced Configuration

### 🦴 Custom Bone Names
If using different body mods, update bone names to match:
- Check your body mod's skeleton for exact bone names
- Use NifScope or similar tools to identify bone hierarchies

### ⚡ Performance Tuning
The monitoring frequency defaults to a 50ms interval (≈20 FPS) for an optimal balance of responsiveness and performance. You can tune that value via `general.intervalMs` in `config.json` or at runtime using the `KnowYourLimits.SetTickInterval` Papyrus function to lower CPU usage or increase responsiveness.

## 📜 Version History

- **v0.2.0**: Rewrote from scales to position translations
- **v0.1.1**: Adjusted config
- **v0.1.0**: Initial release with OStim NG integration
