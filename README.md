# 🎯 Know Your Limits

A realistic collision detection mod for Skyrim Special Edition that adds dynamic size compatibility checks during intimate animations.

## 📖 What This Mod Does

**Know Your Limits** introduces realistic physical limitations to intimate encounters in Skyrim. The mod monitors the positions of anatomical bones during animations and automatically scales down oversized anatomy when it would realistically cause physical limitations or impossibilities.

### ⚙️ Core Mechanics

The mod works by:

1. **🔍 Real-time Bone Monitoring**: Uses SKSE to track the 3D positions of specific bones during animations
2. **📐 Directional Penetration Detection**: Calculates how far anatomy has penetrated beyond realistic limits in the forward direction
3. **📏 Dynamic Scaling**: Automatically scales down oversized anatomy when penetration exceeds configured thresholds
4. **🔄 Automatic Restoration**: Restores original sizes when animations end or when anatomy is no longer over-penetrating

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
- **Duration**: How long to monitor (in seconds, 0 = indefinite)
- **Threshold**: Distance threshold for scaling (negative values allow pre-emptive scaling)

### 🛑 `StopBoneMonitor`
Stops monitoring for specified actors or all actors if none specified.

### ♻️ `ResetScaledBones`
Restores original bone scales for specified actors or all actors if none specified.

### 🔬 Technical Details

- **📊 Penetration Calculation**: Uses directional vectors to determine how far anatomy extends beyond the target point
- **⛓️ Cascade Scaling**: When the threshold is breached, all bones from the penetration point to the tip are scaled
- **🔒 Thread Safety**: All operations are queued on the UI thread to prevent crashes
- **⚡ Performance Optimized**: Monitoring runs at 4 FPS maximum to minimize performance impact

## 📝 Configuration

The mod uses JSON configuration files located in `SKSE/plugins/TT_KnowYourLimits/`:

### 🎛️ Main Configuration (`config.json`)

#### 🎭 Action Definitions
```json
"oral": {
    "actions": ["blowjob", "deepthroat"],
    "bone": "NPC Head [Head]"
},
"vaginal": {
    "actions": ["vaginalsex"],
    "bone": "NPC Pelvis [Pelv]"
},
"anal": {
    "actions": ["analsex"],
    "bone": "NPC Spine [Spn0]"
}
```

- **actions**: Array of animation types that trigger this monitoring mode
- **bone**: Target bone name for collision detection

#### 📦 Size-Based Configurations
```json
"sizes": {
    "default": {
        "oral": {
            "penisBones": ["NPC Genitals04 [Gen04]", "NPC Genitals05 [Gen05]", "NPC Genitals06 [Gen06]"],
            "threshold": -2.0
        }
    },
    "4": {
        "oral": {
            "penisBones": ["NPC Genitals03 [Gen03]", "NPC Genitals04 [Gen04]", "NPC Genitals05 [Gen05]", "NPC Genitals06 [Gen06]"],
            "threshold": -2.0
        }
    }
}
```

- **Size Keys**: "default" applies to all sizes, specific numbers (like "4") override for that size class. You can apply different set of bones and thresholds based on detected size. For example regular and down might need only to bones, large 3 bones and xlarge 4 or more.
- **penisBones**: Array of bone names to monitor (from base to tip). Include only the bones you want to scale down. You should use at least 2 bones for detecting direction.
- **threshold**: Distance threshold for scaling
  - Positive values: Anatomy must penetrate this far beyond the target before scaling
  - Negative values: Scaling occurs this distance before reaching the target (pre-emptive)
  - Zero: Scaling occurs exactly at the target point

#### 📏 Threshold Guidelines
- **👄 Oral**: Typically -2.0 to 0.0 (pre-emptive scaling for realism)
- **🔸 Vaginal**: Typically 5.0 to 8.0 (allows some depth before limiting)
- **🔹 Anal**: Typically 4.0 to 6.0 (slightly more restrictive than vaginal)

### 🚫 Animation Exclusions (`excludeAnimations.json`)

```json
{
    "animationsIds": ["animation_name_1", "animation_name_2"]
}
```

Add animation IDs to this list to exclude them from monitoring. Useful for:
- ✨ Fantasy/magical animations where realism shouldn't apply
- ⚠️ Animations with special positioning that conflicts with the monitoring
- 🐛 Broken or problematic animations

## 📦 Dependencies

### ✅ Required
- **SKSE64** (Skyrim Script Extender)
- **PapyrusUtil** - For data storage and manipulation
- **TNG Framework** - For penis size detection

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
- ✅ **Safe to uninstall** (will restore all scaled bones automatically)
- ✅ Compatible with most body mods and animation frameworks
- ⚠️ May conflict with other mods that manipulate bone scales

## 🔧 Troubleshooting

### ❓ Common Issues

**🔴 Scaling not working**: 
- Check that bone names in config match your body mod
- Verify animation is supported (check excludeAnimations.json)
- Ensure TNG Framework is detecting sizes correctly

**⚠️ Performance issues**:
- Monitoring is limited to 4 FPS for performance
- Large numbers of simultaneous animations may cause brief lag

**🔄 Bones not restoring**:
- Try save and reload game to force restoration

## 🛠️ Advanced Configuration

### 📐 Custom Size Classes
Add new size entries by copying the structure:
```json
"6": {
    "oral": {
        "penisBones": ["NPC Genitals02 [Gen02]", "...", "..."],
        "threshold": -1.0
    }
}
```

### 🦴 Custom Bone Names
If using different body mods, update bone names to match:
- Check your body mod's skeleton for exact bone names
- Use NifScope or similar tools to identify bone hierarchies

### ⚡ Performance Tuning
The monitoring frequency is hardcoded to 250ms intervals (4 FPS) for optimal balance of responsiveness and performance.

## 📜 Version History

- **v1.0**: Initial release with OStim NG integration
