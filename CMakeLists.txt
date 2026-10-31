cmake_minimum_required(VERSION 3.24)

project(PreyVR LANGUAGES C CXX)

option(PREYVR_BUILD_TESTS "Build deterministic math/unit tests" ON)
option(PREYVR_BUILD_TOOLS "Build local research helpers" ON)
option(PREYVR_BUILD_SMOKE_DLL "Build PreyVR.dll, the signature-gated mod, and the tests that compile its sources" ON)
option(PREYVR_BUILD_OPENXR_LOADER "Package the x64 OpenXR loader for preflight-only bootstrap work" ON)
option(PREYVR_REQUIRE_GAME_BASELINE "Fail tests when the researched Prey installation is unavailable" OFF)
set(PREYVR_BUILD_VERSION "0.6.0-preview")
set(PREYVR_SUPPORTED_PREYDLL_SHA256 "7D6E322F61B28331095A400F0BA5F09BD9A39C57BF993602285DD6ACB05311A7")
set(PREYVR_MINHOOK_GIT_COMMIT "c3fcafdc10146beb5919319d0683e44e3c30d537" CACHE STRING "Pinned MinHook commit (v1.3.4)")
set(PREYVR_OPENXR_GIT_COMMIT "64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec" CACHE STRING "Pinned OpenXR-SDK commit (release-1.1.60)")
set(PREYVR_IMGUI_GIT_COMMIT "f1cc2ae15e53a861a874c3034aae6798fde194ab" CACHE STRING "Pinned Dear ImGui commit (v1.92.9b)")
set(
    PREYVR_GAME_RELEASE_DIR
    ""
    CACHE PATH
    "Prey release directory used by the read-only build doctor; empty finds the Steam install"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Deterministic output. Without /Brepro the linker writes the wall-clock link
    # time into IMAGE_FILE_HEADER.TimeDateStamp, so every relink produces a new
    # SHA-256 even from identical source -- see FAILURE_REGISTRY F-005, where a
    # documented artifact hash became unreproducible and the binary a live-test
    # procedure named was lost. /Brepro replaces the timestamp with a content
    # hash. These binaries carry no CODEVIEW debug entry, so no PDB signature is
    # embedded and the timestamp is expected to be the only varying field.
    add_link_options($<$<C_COMPILER_ID:MSVC>:/Brepro> $<$<CXX_COMPILER_ID:MSVC>:/Brepro>)
endif()

include(FetchContent)
if(PREYVR_BUILD_SMOKE_DLL AND WIN32)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG ${PREYVR_MINHOOK_GIT_COMMIT}
        GIT_SHALLOW FALSE
    )
    if(PREYVR_BUILD_OPENXR_LOADER)
        set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
        set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
        set(DYNAMIC_LOADER ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            openxr
            GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
            GIT_TAG ${PREYVR_OPENXR_GIT_COMMIT}
            GIT_SHALLOW FALSE
        )
        FetchContent_MakeAvailable(minhook openxr)
    else()
        FetchContent_MakeAvailable(minhook)
    endif()
    # Dear ImGui draws the VR Options panel. It has no
    # CMake project of its own, so MakeAvailable only fetches it; the library is
    # declared below. No demo, no Win32 clipboard/IME or shell calls: it is fed
    # input by the mod and never touches the game's window.
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG ${PREYVR_IMGUI_GIT_COMMIT}
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(imgui)
    add_library(preyvr_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
    )
    target_include_directories(preyvr_imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
    target_compile_definitions(preyvr_imgui PUBLIC
        IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS
        IMGUI_DISABLE_WIN32_FUNCTIONS IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
    target_link_libraries(preyvr_imgui PUBLIC d3d11 d3dcompiler)
endif()

configure_file(
    cmake/Version.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h"
    @ONLY
)

# Engine-specific state remains in small typed contracts. The game-facing
# target is PreyVR.dll, which stays off unless every landmark matches. src is
# the one include root: every include names its file from there.
add_library(preyvr_headers INTERFACE)
target_include_directories(preyvr_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(preyvr_core STATIC
    src/body/ik/AnimIk.cpp
    src/body/HandPose.cpp
    src/body/ArmPose.cpp
    src/body/BodyRig.cpp
    src/body/RoomScale.cpp
    src/body/BodyTurn.cpp
    src/physics/contact/ContactQuery.cpp
    src/physics/contact/SurfaceStop.cpp
    src/physics/swing/SwingStrike.cpp
    src/diagnostics/overlay/DebugDraw.cpp
    src/diagnostics/overlay/DebugOverlayScene.cpp
    src/aim/ShotRay.cpp
    src/render/stereo/NearParticles.cpp
    src/render/stereo/MarkerDepth.cpp
    src/aim/WeaponRigAlignment.cpp
    src/render/camera/CameraEdit.cpp
    src/engine/console/ConsolePolicy.cpp
    src/diagnostics/capture/DebugRegisters.cpp
    src/controls/input/ControllerProfiles.cpp
    src/controls/input/ActionMapWalk.cpp
    src/engine/EngineMap.cpp
    src/diagnostics/capture/FrameDump.cpp
    src/engine/hooks/FrameObserver.cpp
    src/controls/input/InputEvent.cpp
    src/controls/input/InputQueue.cpp
    src/physics/hands/InteractionUse.cpp
    src/physics/hands/HandCarry.cpp
    src/physics/hands/GrenadeThrow.cpp
    src/ui/hud/WristHolo.cpp
    src/controls/locomotion/Locomotion.cpp
    src/controls/menus/MenuNavigator.cpp
    src/ui/screens/UiPanel.cpp
    src/aim/MotionController.cpp
    src/aim/TwoHandedAim.cpp
    src/ui/options/VrOptions.cpp
    src/xr/OpenXRBootstrap.cpp
    src/diagnostics/probes/RuntimeSnapshot.cpp
    src/render/camera/StereoCamera.cpp
    src/render/camera/StereoFrame.cpp
    src/base/VrMath.cpp
    src/xr/FrameTiming.cpp
    src/xr/FrustumCoverage.cpp
    src/aim/WeaponAim.cpp
    src/xr/XrFrameContract.cpp
    src/xr/XrHold.cpp
    src/xr/XrSwapchainFormat.cpp
)
target_link_libraries(preyvr_core PUBLIC preyvr_headers)

function(preyvr_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()
preyvr_enable_warnings(preyvr_core)

# Sets <out_var> to the post-build COMMAND that copies openxr_loader.dll beside
# <target>, so callers can put it in a custom command with other steps.
function(preyvr_openxr_loader_copy out_var target)
    set(${out_var}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:openxr_loader>
            $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:openxr_loader>
        PARENT_SCOPE
    )
endfunction()

# preyvr_add_test(<ctest-name> <source> [TARGET <exe>] [SOURCES ...]
#                 [INCLUDES ...] [DEFS ...] [LIBS ...] [ARGS ...])
# Builds <source> plus SOURCES into <exe> (default preyvr_<ctest-name>_tests)
# and registers it with CTest as <ctest-name>, run with ARGS. LIBS replaces the
# default preyvr_core, so a test that does not link the core lists what it does.
# Tools and docs name the executables, so keep TARGET when renaming a test.
function(preyvr_add_test name source)
    cmake_parse_arguments(PARSE_ARGV 2 arg "DLL" "TARGET" "SOURCES;INCLUDES;DEFS;LIBS;ARGS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "preyvr_add_test(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    set(target preyvr_${name}_tests)
    if(arg_TARGET)
        set(target ${arg_TARGET})
    endif()
    # DLL: the test compiles DLL sources, so it gets the include root and defines
    # and builds the same translation unit the DLL ships.
    if(arg_DLL)
        list(APPEND arg_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src")
        list(APPEND arg_DEFS WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    if(NOT DEFINED arg_LIBS)
        set(arg_LIBS preyvr_core)
    endif()
    add_executable(${target} ${source} ${arg_SOURCES})
    # Tests sit in a folder per category; their shared helpers are in tests/.
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests" ${arg_INCLUDES})
    if(arg_DEFS)
        target_compile_definitions(${target} PRIVATE ${arg_DEFS})
    endif()
    target_link_libraries(${target} PRIVATE ${arg_LIBS})
    preyvr_enable_warnings(${target})
    add_test(NAME ${name} COMMAND ${target} ${arg_ARGS})
endfunction()

if((PREYVR_BUILD_TOOLS OR PREYVR_BUILD_SMOKE_DLL) AND WIN32)
    add_executable(preyvr_injector tools/injector/main.cpp)
    target_compile_definitions(preyvr_injector PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    preyvr_enable_warnings(preyvr_injector)
    add_executable(preyvr_engine_map_probe tools/engine_map_probe/main.cpp)
    target_compile_definitions(preyvr_engine_map_probe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(preyvr_engine_map_probe PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_engine_map_probe)
endif()

if(PREYVR_BUILD_SMOKE_DLL AND WIN32)
    add_library(preyvr_win32_bootstrap STATIC
        src/xr/OpenXRPreflightWin32.cpp
        src/diagnostics/probes/RuntimeSnapshotWin32.cpp
    )
    target_compile_definitions(preyvr_win32_bootstrap PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(preyvr_win32_bootstrap PUBLIC preyvr_core advapi32)
    preyvr_enable_warnings(preyvr_win32_bootstrap)

    # The DLL's sources, compiled once: PreyVR.dll links every object, in this
    # order, and the command channel's test links all but four (see
    # command_channel below). They are compiled as the SHARED target compiled
    # them, its export symbol and _WINDLL included, so the objects are the same.
    add_library(preyvr_dll_objects OBJECT
        src/aim/AimTakeover.cpp
        src/aim/GameplayFrame.cpp
        src/aim/TwoHandHold.cpp
        src/equipment/psi/PsiMedkit.cpp
        src/body/ik/AnimIkTakeover.cpp
        src/body/ik/AnimIkRig.cpp
        src/body/ik/AnimIkHand.cpp
        src/body/ik/AnimIkArms.cpp
        src/body/ik/AnimIkBody.cpp
        src/body/ik/AnimIkReports.cpp
        src/app/Bootstrap.cpp
        src/app/Exports.cpp
        src/app/VrMode.cpp
        src/controls/input/ActionMaps.cpp
        src/ui/options/VrOptionsLayer.cpp
        src/ui/options/VrOptionsUi.cpp
        src/ui/options/VrOptionsRuntime.cpp
        src/equipment/psi/Psychoscope.cpp
        src/equipment/body/BodyEquipmentLane.cpp
        src/body/TorsoLane.cpp
        src/body/RoomScaleLane.cpp
        src/diagnostics/overlay/DebugOverlay.cpp
        src/aim/ShotRayLane.cpp
        src/aim/ReticleRayHook.cpp
        src/physics/hands/InteractionLane.cpp
        src/physics/hands/CarryLane.cpp
        src/physics/contact/ContactLane.cpp
        src/physics/contact/SurfaceStopLane.cpp
        src/physics/swing/SwingLane.cpp
        src/equipment/weapon/WeaponDraw.cpp
        src/equipment/weapon/WeaponSound.cpp
        src/physics/hands/GrenadeLane.cpp
        src/body/ArmsLane.cpp
        src/diagnostics/commands/MemoryCommands.cpp
        src/equipment/body/SlotFeedbackLane.cpp
        src/ui/hud/NativeWristTexture.cpp
        src/ui/hud/WristLane.cpp
        src/ui/hud/HudLayer.cpp
        src/xr/LayerSwapchain.cpp
        src/ui/screens/UiPointer.cpp
        src/ui/screens/InventoryPointerCapture.cpp
        src/engine/hooks/MainRenderHook.cpp
        src/render/camera/StereoEyeCamera.cpp
        src/render/camera/EyeRecords.cpp
        src/engine/console/ConsoleBridgeWin32.cpp
        src/engine/fixes/CullPrepareGuard.cpp
        src/diagnostics/probes/FrameProbe.cpp
        src/diagnostics/capture/OverflowCapture.cpp
        src/engine/fixes/FocusHold.cpp
        src/ui/screens/LoadingScreen.cpp
        src/controls/input/ControllerPrompts.cpp
        src/diagnostics/capture/DebugWatch.cpp
        src/app/DllMain.cpp
        src/diagnostics/capture/FrameCaptureWin32.cpp
        src/engine/hooks/FrameObserverHook.cpp
        src/app/FrameSchedule.cpp
        src/render/camera/HeadTrackingHook.cpp
        src/ui/hud/HudBridge.cpp
        src/diagnostics/commands/ResearchHotkeys.cpp
        src/diagnostics/probes/IkTargetProbe.cpp
        src/controls/input/InputPath.cpp
        src/controls/input/InputPost.cpp
        src/engine/hooks/MinHookInit.cpp
        src/controls/locomotion/MoveLane.cpp
        src/controls/input/StickReread.cpp
        src/diagnostics/commands/CommandChannel.cpp
        src/app/WorkerThread.cpp
        src/render/stereo/NearFovOverride.cpp
        src/render/stereo/NearViewStereo.cpp
        src/render/stereo/NearParticleStereo.cpp
        src/render/stereo/MarkerStereo.cpp
        src/render/stereo/StarfieldStereo.cpp
        src/ui/screens/ExamineCamera.cpp
        src/render/camera/ZoomLane.cpp
        src/equipment/head/HeadLampLane.cpp
        src/equipment/head/HelmetLane.cpp
        src/render/stereo/BodyDepthHook.cpp
        src/render/stereo/DualSceneStereo.cpp
        src/diagnostics/probes/TemporalAaProbe.cpp
        src/render/taa/TemporalAaLane.cpp
        src/render/stereo/DualOverlay.cpp
        src/render/stereo/VideoOverlay.cpp
        src/aim/WeaponAttachment.cpp
        src/ui/hud/ReticleFollow.cpp
        src/controls/input/XrInput.cpp
        src/controls/input/Haptics.cpp
        src/physics/contact/SceneQueryLane.cpp
        src/physics/contact/PhysicalInteractionsLane.cpp
        src/xr/XrSessionHost.cpp
        src/xr/XrSessionLifecycle.cpp
        src/xr/XrEyePair.cpp
        src/xr/XrMenuPanel.cpp
        src/xr/XrOverlayLayers.cpp
        src/xr/XrHoldFrames.cpp
        src/xr/XrSessionReports.cpp
        src/base/Logger.cpp
    )
    target_include_directories(preyvr_dll_objects PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(preyvr_dll_objects PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        preyvr_EXPORTS ${CMAKE_CXX_SHARED_LIBRARY_COMPILE_DEFINITIONS})
    # For their headers and definitions; PreyVR.dll names the libraries it links.
    target_link_libraries(preyvr_dll_objects PRIVATE preyvr_core preyvr_win32_bootstrap preyvr_imgui minhook)
    preyvr_enable_warnings(preyvr_dll_objects)

    add_library(preyvr SHARED $<TARGET_OBJECTS:preyvr_dll_objects>)
    target_link_libraries(preyvr PRIVATE preyvr_core preyvr_win32_bootstrap preyvr_imgui minhook bcrypt psapi shell32 d3d11 dxgi d3dcompiler gdi32)
    if(PREYVR_BUILD_OPENXR_LOADER AND TARGET openxr_loader)
        # The in-process session host needs the loader. The DLL already ships
        # openxr_loader.dll beside itself for the preflight, so this adds a link
        # dependency rather than a new deployment requirement.
        target_link_libraries(preyvr PRIVATE openxr_loader delayimp)
        target_link_libraries(preyvr_dll_objects PRIVATE openxr_loader)
        target_compile_definitions(preyvr_dll_objects PRIVATE XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11)
        # **Delay-loaded on purpose.** A hard import would make PreyVR.dll fail to
        # load at all when openxr_loader.dll is not beside it -- the landmark gate
        # would never run, nothing would reach the log, and the failure would read
        # as "the DLL is broken" rather than "a dependency is missing". Injection
        # is precisely where an adjacent DLL may not be on the search path.
        # Delay-loading keeps the fail-closed lifecycle intact and turns a missing
        # loader into a clean refusal inside StartXrSession instead.
        target_link_options(preyvr PRIVATE /DELAYLOAD:openxr_loader.dll)
    endif()
    set_target_properties(preyvr PROPERTIES OUTPUT_NAME PreyVR)
    if(MSVC)
        # **A PDB for every build** (docs/ENGINE-INSTABILITY.md): without one a
        # crash dump's PreyVR frames can only be matched by disassembly. Debug
        # information in the objects, PreyVR.pdb beside the DLL; /DEBUG would
        # otherwise turn off /OPT:REF and /OPT:ICF, so the code stays as it was.
        target_compile_options(preyvr_dll_objects PRIVATE /Z7)
        target_link_options(preyvr PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
    endif()
    if(TARGET preyvr_engine_map_probe)
        add_dependencies(preyvr preyvr_engine_map_probe)
    endif()
    # With the loader, one post-build step copies it beside the DLL first and
    # the manifest records it.
    set(preyvr_loader_copy "")
    set(preyvr_manifest_loader "")
    if(PREYVR_BUILD_OPENXR_LOADER)
        add_dependencies(preyvr openxr_loader)
        preyvr_openxr_loader_copy(preyvr_loader_copy preyvr)
        set(preyvr_manifest_loader
            -DPREYVR_OPENXR_LOADER=$<TARGET_FILE_DIR:preyvr>/$<TARGET_FILE_NAME:openxr_loader>)
    endif()
    add_custom_command(
        TARGET preyvr
        POST_BUILD
        ${preyvr_loader_copy}
        COMMAND
            ${CMAKE_COMMAND}
            -DPREYVR_DLL=$<TARGET_FILE:preyvr>
            ${preyvr_manifest_loader}
            -DPREYVR_MANIFEST=$<TARGET_FILE_DIR:preyvr>/PreyVR-build-manifest.txt
            -DPREYVR_VERSION=${PREYVR_BUILD_VERSION}
            -DPREYVR_CONFIGURATION=$<CONFIG>
            -DPREYVR_EXPECTED_PREYDLL_SHA256=${PREYVR_SUPPORTED_PREYDLL_SHA256}
            -DPREYVR_ENGINE_MAP_PROBE=$<TARGET_FILE:preyvr_engine_map_probe>
            -DPREYVR_MINHOOK_COMMIT=${PREYVR_MINHOOK_GIT_COMMIT}
            -DPREYVR_OPENXR_COMMIT=${PREYVR_OPENXR_GIT_COMMIT}
            -DPREYVR_IMGUI_COMMIT=${PREYVR_IMGUI_GIT_COMMIT}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WriteBuildManifest.cmake"
        VERBATIM
    )
endif()

if(PREYVR_BUILD_TOOLS AND WIN32)
    add_executable(preyvr_dxgi_probe tools/dxgi_probe/main.cpp)
    target_link_libraries(preyvr_dxgi_probe PRIVATE d3d11 dxgi user32)
    preyvr_enable_warnings(preyvr_dxgi_probe)

    add_executable(preyvr_frame_diff tools/frame_diff/main.cpp)
    target_link_libraries(preyvr_frame_diff PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_frame_diff)

    # Answers the adapter-LUID question out of process, so it needs no running
    # Prey and no headset to build. Skipped when the loader is not being built,
    # since without it there is nothing to ask.
    if(PREYVR_BUILD_OPENXR_LOADER AND TARGET openxr_loader)
        # The full session lifecycle, runnable against xr-sim without a headset.
        add_executable(preyvr_xr_session_probe tools/xr_session_probe/main.cpp)
        target_compile_definitions(preyvr_xr_session_probe PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11)
        target_link_libraries(preyvr_xr_session_probe PRIVATE preyvr_core openxr_loader d3d11 dxgi)
        preyvr_enable_warnings(preyvr_xr_session_probe)
        preyvr_openxr_loader_copy(preyvr_loader_copy preyvr_xr_session_probe)
        add_custom_command(TARGET preyvr_xr_session_probe POST_BUILD ${preyvr_loader_copy} VERBATIM)

        add_executable(preyvr_xr_adapter_probe tools/xr_adapter_probe/main.cpp)
        target_compile_definitions(preyvr_xr_adapter_probe PRIVATE
            WIN32_LEAN_AND_MEAN NOMINMAX XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11)
        target_link_libraries(preyvr_xr_adapter_probe PRIVATE openxr_loader d3d11 dxgi)
        preyvr_enable_warnings(preyvr_xr_adapter_probe)
        preyvr_openxr_loader_copy(preyvr_loader_copy preyvr_xr_adapter_probe)
        add_custom_command(TARGET preyvr_xr_adapter_probe POST_BUILD ${preyvr_loader_copy} VERBATIM)
    endif()
endif()

if(PREYVR_BUILD_TESTS)
    enable_testing()

    preyvr_add_test(vr_math tests/base/VrMathTests.cpp)
    preyvr_add_test(engine_map tests/engine/EngineMapTests.cpp)
    preyvr_add_test(runtime_snapshot tests/diagnostics/RuntimeSnapshotTests.cpp)
    preyvr_add_test(xr_swapchain_format tests/xr/XrSwapchainFormatTests.cpp)
    preyvr_add_test(xr_frame_contract tests/xr/XrFrameContractTests.cpp)
    preyvr_add_test(stereo_frame tests/render/StereoFrameTests.cpp)
    preyvr_add_test(comfort_controls tests/controls/ComfortControlsTests.cpp)
    preyvr_add_test(motion_controller tests/aim/MotionControllerTests.cpp)
    preyvr_add_test(two_handed_aim tests/aim/TwoHandedAimTests.cpp)
    preyvr_add_test(vr_options_contracts tests/ui/VrOptionsTests.cpp TARGET preyvr_vr_options_tests)
    preyvr_add_test(controller_profiles tests/controls/ControllerProfilesTests.cpp)
    preyvr_add_test(action_map_walk tests/controls/ActionMapWalkTests.cpp)
    preyvr_add_test(release_gate tests/controls/ReleaseGateTests.cpp)
    preyvr_add_test(body_equipment_contracts tests/equipment/BodyEquipmentTests.cpp TARGET preyvr_body_equipment_tests)
    preyvr_add_test(anim_ik tests/body/AnimIkTests.cpp)
    preyvr_add_test(hand_pose tests/body/HandPoseTests.cpp)
    preyvr_add_test(arm_pose tests/body/ArmPoseTests.cpp)
    preyvr_add_test(body_turn tests/body/BodyTurnTests.cpp)
    preyvr_add_test(zero_g tests/controls/ZeroGTests.cpp)
    preyvr_add_test(body_rig tests/body/BodyRigTests.cpp)
    preyvr_add_test(room_scale tests/body/RoomScaleTests.cpp)
    preyvr_add_test(debug_draw tests/diagnostics/DebugDrawTests.cpp)
    preyvr_add_test(shot_ray tests/aim/ShotRayTests.cpp)
    preyvr_add_test(near_particles tests/render/NearParticlesTests.cpp)
    preyvr_add_test(marker_depth tests/render/MarkerDepthTests.cpp)
    preyvr_add_test(starfield_depth tests/render/StarfieldDepthTests.cpp)
    preyvr_add_test(body_depth tests/render/BodyDepthTests.cpp)
    preyvr_add_test(near_motion tests/render/NearMotionTests.cpp)
    preyvr_add_test(world_motion tests/render/WorldMotionTests.cpp)
    preyvr_add_test(near_clip tests/render/NearClipTests.cpp)
    preyvr_add_test(view_zoom tests/render/ViewZoomTests.cpp)
    preyvr_add_test(examine_pullback tests/ui/ExaminePullbackTests.cpp)
    preyvr_add_test(examine_ray tests/aim/ExamineRayTests.cpp)
    preyvr_add_test(video_letterbox tests/render/VideoLetterboxTests.cpp)
    preyvr_add_test(hand_carry tests/physics/HandCarryTests.cpp)
    preyvr_add_test(grenade_throw tests/physics/GrenadeThrowTests.cpp)
    preyvr_add_test(wrist_holo tests/ui/WristHoloTests.cpp)
    preyvr_add_test(interaction_use tests/physics/InteractionUseTests.cpp)
    preyvr_add_test(weapon_rig_alignment tests/aim/WeaponRigAlignmentTests.cpp)
    preyvr_add_test(weapon_aim tests/aim/WeaponAimTests.cpp)
    preyvr_add_test(scene_query_contracts tests/physics/SceneQueryTests.cpp TARGET preyvr_scene_query_tests)
    preyvr_add_test(physical_interactions tests/physics/PhysicalInteractionsTests.cpp)
    preyvr_add_test(contact_native tests/physics/ContactNativeTests.cpp)
    preyvr_add_test(swing_native tests/physics/SwingNativeTests.cpp)
    preyvr_add_test(swing_strike tests/physics/SwingStrikeTests.cpp)
    preyvr_add_test(weapon_draw_native tests/equipment/WeaponDrawNativeTests.cpp)
    preyvr_add_test(contact_query tests/physics/ContactQueryTests.cpp)
    preyvr_add_test(surface_stop tests/physics/SurfaceStopTests.cpp)
    preyvr_add_test(psi_medkit_contracts tests/equipment/PsiMedkitTests.cpp TARGET preyvr_psi_medkit_tests)
    preyvr_add_test(frustum_coverage tests/xr/FrustumCoverageTests.cpp)
    preyvr_add_test(frame_timing tests/xr/FrameTimingTests.cpp)
    preyvr_add_test(input_event tests/controls/InputEventTests.cpp)
    preyvr_add_test(input_queue tests/controls/InputQueueTests.cpp)
    preyvr_add_test(menu_navigator tests/controls/MenuNavigatorTests.cpp)
    preyvr_add_test(menu_hold tests/controls/MenuHoldTests.cpp)
    preyvr_add_test(render_contract tests/render/RenderContractTests.cpp)
    preyvr_add_test(ui_panel tests/ui/UiPanelTests.cpp)
    preyvr_add_test(position_ring tests/render/PositionRingTests.cpp)
    preyvr_add_test(xr_hold tests/xr/XrHoldTests.cpp)
    preyvr_add_test(hud_parts tests/ui/HudPartsTests.cpp)
    preyvr_add_test(locomotion tests/controls/LocomotionTests.cpp)
    preyvr_add_test(stereo_camera tests/render/StereoCameraTests.cpp)
    preyvr_add_test(camera_edit tests/render/CameraEditTests.cpp)
    preyvr_add_test(frame_dump tests/diagnostics/FrameDumpTests.cpp)
    preyvr_add_test(debug_registers tests/diagnostics/DebugRegistersTests.cpp)
    preyvr_add_test(console_policy tests/engine/ConsolePolicyTests.cpp)
    preyvr_add_test(runtime_bootstrap tests/engine/RuntimeBootstrapTests.cpp)

    if(PREYVR_BUILD_SMOKE_DLL AND WIN32)
        preyvr_add_test(slot_feedback tests/equipment/SlotFeedbackTests.cpp
            DLL
            LIBS preyvr_core gdi32 user32)
        preyvr_add_test(psi_medkit_runtime tests/equipment/PsiMedkitRuntimeTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(physical_runtime tests/physics/PhysicalRuntimeTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(contact_lane tests/physics/ContactLaneTests.cpp
            DLL
            LIBS preyvr_core)
        preyvr_add_test(surface_stop_lane tests/physics/SurfaceStopLaneTests.cpp
            DLL
            LIBS preyvr_core)
        preyvr_add_test(hand_world tests/body/HandWorldTests.cpp
            DLL
            LIBS preyvr_core)
        preyvr_add_test(swing_lane tests/physics/SwingLaneTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(weapon_draw tests/equipment/WeaponDrawTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(weapon_sound tests/equipment/WeaponSoundTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(starfield_stereo tests/render/StarfieldStereoTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(examine_camera tests/ui/ExamineCameraTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(zoom_lane tests/render/ZoomLaneTests.cpp
            DLL
            LIBS preyvr_core)
        preyvr_add_test(head_lamp tests/equipment/HeadLampTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(helmet tests/equipment/HelmetTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(temporal_aa_probe tests/diagnostics/TemporalAaProbeTests.cpp
            DLL
            LIBS preyvr_core d3d11)
        preyvr_add_test(temporal_aa tests/render/TemporalAaTests.cpp)
        preyvr_add_test(temporal_aa_lane tests/render/TemporalAaLaneTests.cpp
            DLL
            LIBS preyvr_core d3d11)
        preyvr_add_test(body_depth_hook tests/render/BodyDepthHookTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(video_overlay tests/render/VideoOverlayTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(ui_pointer_dispatch tests/ui/UiPointerTests.cpp TARGET preyvr_ui_pointer_tests
            DLL)
        # The VR Options panel, drawn by the real ImGui backend on WARP.
        preyvr_add_test(vr_options_ui tests/ui/VrOptionsUiTests.cpp SOURCES src/ui/options/VrOptionsUi.cpp
            DLL
            LIBS preyvr_core preyvr_imgui d3d11)
        preyvr_add_test(native_wrist tests/ui/NativeWristTests.cpp SOURCES src/ui/hud/NativeWristTexture.cpp
            DLL
            LIBS preyvr_core d3d11 d3dcompiler)
        preyvr_add_test(vr_options_runtime tests/ui/VrOptionsRuntimeTests.cpp
            DLL)

        preyvr_add_test(inventory_pointer_capture tests/ui/InventoryPointerCaptureTests.cpp
            TARGET preyvr_inventory_capture_tests
            DLL
            LIBS minhook)
        preyvr_add_test(near_fov_override tests/render/NearFovOverrideTests.cpp TARGET preyvr_near_fov_tests
            DLL
            LIBS minhook)
        preyvr_add_test(frame_probe tests/diagnostics/FrameProbeTests.cpp
            DLL
            LIBS preyvr_headers)
        preyvr_add_test(cull_prepare_guard tests/engine/CullPrepareGuardTests.cpp
            DLL
            LIBS preyvr_headers)
        # Overflows a thread of its own child process and reads what was written.
        preyvr_add_test(overflow_capture tests/diagnostics/OverflowCaptureTests.cpp
            DLL
            LIBS preyvr_headers)
        preyvr_add_test(focus_hold tests/engine/FocusHoldTests.cpp
            DLL
            LIBS preyvr_headers minhook user32)
        preyvr_add_test(controller_prompts tests/controls/ControllerPromptsTests.cpp
            DLL)
        preyvr_add_test(hook_install tests/engine/HookInstallTests.cpp
            DLL
            LIBS minhook)
        # The offline VR scheme replays: the real MoveLane, AimTakeover, HudBridge and
        # ReticleFollow sources, with the game, input and hook boundaries stubbed.
        foreach(review IN ITEMS scheme aim hud_queue reticle)
            preyvr_add_test(${review}_review tools/re/vr_scheme_review/${review}_review.cpp
                DLL
                LIBS preyvr_core minhook)
        endforeach()

        # Links nothing and loads the built DLL, so it stays outside preyvr_add_test.
        add_executable(preyvr_dll_smoke_host tests/DllSmokeHost.cpp)
        preyvr_enable_warnings(preyvr_dll_smoke_host)
        add_test(NAME dll_load_fail_closed COMMAND preyvr_dll_smoke_host $<TARGET_FILE:preyvr>)
        set_tests_properties(
            dll_load_fail_closed
            PROPERTIES
                ENVIRONMENT "PREYVR_LOG_PATH=${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/preyvr-smoke-host.log"
                PASS_REGULAR_EXPRESSION "dll_smoke_host status=unsupported_as_expected"
        )

        if(PREYVR_BUILD_OPENXR_LOADER)
            preyvr_add_test(haptics_contracts tests/controls/HapticsTests.cpp TARGET preyvr_haptics_tests
                SOURCES src/controls/input/Haptics.cpp
                INCLUDES $<TARGET_PROPERTY:openxr_loader,INTERFACE_INCLUDE_DIRECTORIES>
                DLL)
            preyvr_add_test(layer_swapchain tests/xr/LayerSwapchainTests.cpp
                SOURCES src/xr/LayerSwapchain.cpp
                INCLUDES $<TARGET_PROPERTY:openxr_loader,INTERFACE_INCLUDE_DIRECTORIES>
                DLL
                DEFS XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
                LIBS preyvr_headers d3d11)
            # The command channel's replies, pinned. The test includes the channel
            # and links the DLL's other objects but DllMain, with the logger and
            # MinHook's initialiser stubbed. Built apart from the copied loader, so
            # a stray OpenXR call fails to delay-load instead of finding a runtime.
            preyvr_add_test(command_channel tests/diagnostics/CommandChannelTests.cpp
                SOURCES "$<FILTER:$<TARGET_OBJECTS:preyvr_dll_objects>,EXCLUDE,/(CommandChannel|DllMain|Logger|MinHookInit)[.]obj$>"
                INCLUDES "${CMAKE_CURRENT_BINARY_DIR}/generated"
                DLL
                DEFS XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
                LIBS preyvr_core preyvr_win32_bootstrap preyvr_imgui minhook bcrypt psapi shell32 d3d11 dxgi
                    d3dcompiler gdi32 openxr_loader delayimp)
            target_link_options(preyvr_command_channel_tests PRIVATE /DELAYLOAD:openxr_loader.dll)
            target_compile_definitions(preyvr_command_channel_tests PRIVATE
                PREYVR_TUNING_DOC="${CMAKE_CURRENT_SOURCE_DIR}/docs/TUNING-COMMANDS.md")
            set_target_properties(preyvr_command_channel_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/command_channel")
            # VR Options' wiring against the real lanes, linked the same way: the
            # test includes VrOptionsRuntime and links the DLL's other objects.
            preyvr_add_test(vr_options_wiring tests/ui/VrOptionsWiringTests.cpp
                SOURCES "$<FILTER:$<TARGET_OBJECTS:preyvr_dll_objects>,EXCLUDE,/(VrOptionsRuntime|DllMain|Logger|MinHookInit)[.]obj$>"
                INCLUDES "${CMAKE_CURRENT_BINARY_DIR}/generated"
                DLL
                DEFS XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
                LIBS preyvr_core preyvr_win32_bootstrap preyvr_imgui minhook bcrypt psapi shell32 d3d11 dxgi
                    d3dcompiler gdi32 openxr_loader delayimp)
            target_link_options(preyvr_vr_options_wiring_tests PRIVATE /DELAYLOAD:openxr_loader.dll)
            set_target_properties(preyvr_vr_options_wiring_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/command_channel")

            preyvr_add_test(openxr_preflight_files tests/xr/OpenXRPreflightWin32Tests.cpp
                TARGET preyvr_openxr_preflight_tests
                LIBS preyvr_win32_bootstrap
                ARGS $<TARGET_FILE:openxr_loader> $<TARGET_FILE:preyvr>)
        endif()
    endif()

    if(PREYVR_BUILD_TOOLS AND WIN32)
        add_test(NAME dxgi_method_probe COMMAND preyvr_dxgi_probe)
        set_tests_properties(
            dxgi_method_probe
            PROPERTIES PASS_REGULAR_EXPRESSION "IDXGISwapChain::Present.*dxgi_rva="
        )
    endif()

    find_program(PREYVR_POWERSHELL_EXECUTABLE NAMES pwsh powershell)
    if(PREYVR_POWERSHELL_EXECUTABLE)
        foreach(suite Resolution Settings Stereo Evidence)
            string(TOLOWER "${suite}" suite_name)
            add_test(NAME launcher_${suite_name} COMMAND "${PREYVR_POWERSHELL_EXECUTABLE}"
                -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/Launcher${suite}.Tests.ps1")
        endforeach()
    endif()

    # The tools/re static verifiers against the installed PreyDll.dll; skipped
    # (77) on a machine without the supported build.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME re_verifiers COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/re/run_verifiers.py")
        set_tests_properties(re_verifiers PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
        # Pure code and DLL code share folders, so nothing about a path keeps them
        # apart: this does.
        add_test(NAME pure_boundary COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_pure_boundary.py")
    endif()
    if(PREYVR_POWERSHELL_EXECUTABLE AND PREYVR_BUILD_SMOKE_DLL AND WIN32)
        add_test(
            NAME smoke_log_parser
            COMMAND
                "${PREYVR_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/Test-PreyVRSmokeLog.ps1"
                -LogPath "${CMAKE_CURRENT_BINARY_DIR}/Testing/Temporary/preyvr-smoke-host.log"
                -ExpectedStatus unsupported
                -ExpectedLandmarkCount 0
        )
        set_tests_properties(
            smoke_log_parser
            PROPERTIES
                DEPENDS dll_load_fail_closed
                PASS_REGULAR_EXPRESSION "smoke_log_check result=pass"
        )
    endif()
    if(PREYVR_POWERSHELL_EXECUTABLE AND PREYVR_BUILD_TOOLS AND WIN32)
        set(PREYVR_DOCTOR_ARGUMENTS -EngineMapProbe $<TARGET_FILE:preyvr_engine_map_probe>)
        if(PREYVR_GAME_RELEASE_DIR)
            list(APPEND PREYVR_DOCTOR_ARGUMENTS -GameReleasePath "${PREYVR_GAME_RELEASE_DIR}")
        endif()
        if(NOT PREYVR_REQUIRE_GAME_BASELINE)
            list(APPEND PREYVR_DOCTOR_ARGUMENTS -AllowMissingGame)
        endif()
        add_test(
            NAME build_doctor
            COMMAND
                "${PREYVR_POWERSHELL_EXECUTABLE}"
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/Invoke-PreyVRDoctor.ps1"
                ${PREYVR_DOCTOR_ARGUMENTS}
        )
        set_tests_properties(
            build_doctor
            PROPERTIES PASS_REGULAR_EXPRESSION "SUMMARY: pass=.*fail=0"
        )
    endif()
endif()
