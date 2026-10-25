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
# target is PreyVR.dll (src/dll), which stays off unless every landmark matches.
add_library(preyvr_headers INTERFACE)
target_include_directories(preyvr_headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(preyvr_core STATIC
    src/common/AimState.cpp
    src/common/AnimIk.cpp
    src/common/HandPose.cpp
    src/common/ArmPose.cpp
    src/common/BodyTurn.cpp
    src/common/ContactQuery.cpp
    src/common/SurfaceStop.cpp
    src/common/SwingStrike.cpp
    src/common/DebugDraw.cpp
    src/common/DebugOverlayScene.cpp
    src/common/ShotRay.cpp
    src/common/NearParticles.cpp
    src/common/MarkerDepth.cpp
    src/common/WeaponRigAlignment.cpp
    src/common/CameraEdit.cpp
    src/common/ConsolePolicy.cpp
    src/common/DebugRegisters.cpp
    src/common/ControllerProfiles.cpp
    src/common/ActionMapWalk.cpp
    src/common/EngineMap.cpp
    src/common/FrameDump.cpp
    src/common/FrameObserver.cpp
    src/common/InputEvent.cpp
    src/common/InputQueue.cpp
    src/common/InteractionQuery.cpp
    src/common/InteractionUse.cpp
    src/common/HandCarry.cpp
    src/common/GrenadeThrow.cpp
    src/common/WristHolo.cpp
    src/common/Locomotion.cpp
    src/common/MenuNavigator.cpp
    src/common/InventoryDepth.cpp
    src/common/UiPanel.cpp
    src/common/MotionController.cpp
    src/common/TwoHandedAim.cpp
    src/common/VrOptions.cpp
    src/common/OpenXRBootstrap.cpp
    src/common/RuntimeSnapshot.cpp
    src/common/StereoCamera.cpp
    src/common/StereoFrame.cpp
    src/common/VrMath.cpp
    src/common/FrameTiming.cpp
    src/common/FrustumCoverage.cpp
    src/common/WeaponAim.cpp
    src/common/WrenchQuery.cpp
    src/common/XrFrameContract.cpp
    src/common/XrHold.cpp
    src/common/XrSwapchainFormat.cpp
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
    # DLL: the test compiles DLL sources, so it gets the DLL's lifecycle include
    # and defines and builds the same translation unit the DLL ships.
    if(arg_DLL)
        list(APPEND arg_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/lifecycle")
        list(APPEND arg_DEFS WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    if(NOT DEFINED arg_LIBS)
        set(arg_LIBS preyvr_core)
    endif()
    add_executable(${target} ${source} ${arg_SOURCES})
    if(arg_INCLUDES)
        target_include_directories(${target} PRIVATE ${arg_INCLUDES})
    endif()
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
        src/dll/OpenXRPreflightWin32.cpp
        src/dll/RuntimeSnapshotWin32.cpp
    )
    target_include_directories(preyvr_win32_bootstrap PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/dll")
    target_compile_definitions(preyvr_win32_bootstrap PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(preyvr_win32_bootstrap PUBLIC preyvr_core advapi32)
    preyvr_enable_warnings(preyvr_win32_bootstrap)

    # The DLL's sources, compiled once: PreyVR.dll links every object, in this
    # order, and the command channel's test links all but four (see
    # command_channel below). They are compiled as the SHARED target compiled
    # them, its export symbol and _WINDLL included, so the objects are the same.
    add_library(preyvr_dll_objects OBJECT
        src/dll/AimTakeover.cpp
        src/dll/GameplayFrame.cpp
        src/dll/TwoHandHold.cpp
        src/dll/PsiMedkit.cpp
        src/dll/AnimIkTakeover.cpp
        src/dll/AnimIkRig.cpp
        src/dll/AnimIkHand.cpp
        src/dll/AnimIkArms.cpp
        src/dll/AnimIkReports.cpp
        src/dll/Bootstrap.cpp
        src/dll/Exports.cpp
        src/dll/VrMode.cpp
        src/dll/UiGuide.cpp
        src/dll/ActionMaps.cpp
        src/dll/VrOptionsLayer.cpp
        src/dll/VrOptionsUi.cpp
        src/dll/VrOptionsRuntime.cpp
        src/dll/Psychoscope.cpp
        src/dll/BodyEquipment.cpp
        src/dll/TorsoLane.cpp
        src/dll/DebugOverlay.cpp
        src/dll/ShotRay.cpp
        src/dll/ReticleRayHook.cpp
        src/dll/InteractionLane.cpp
        src/dll/CarryLane.cpp
        src/dll/ContactLane.cpp
        src/dll/SurfaceStopLane.cpp
        src/dll/SwingLane.cpp
        src/dll/WrenchDraw.cpp
        src/dll/WrenchSound.cpp
        src/dll/GrenadeLane.cpp
        src/dll/ArmsLane.cpp
        src/dll/MemoryCommands.cpp
        src/dll/SlotFeedback.cpp
        src/dll/NativeWristTexture.cpp
        src/dll/WristLane.cpp
        src/dll/HudLayer.cpp
        src/dll/LayerSwapchain.cpp
        src/dll/UiPointer.cpp
        src/dll/InventoryPointerCapture.cpp
        src/dll/MainRenderHook.cpp
        src/dll/StereoEyeCamera.cpp
        src/dll/EyeRecords.cpp
        src/dll/ConsoleBridgeWin32.cpp
        src/dll/CullCameraWatch.cpp
        src/dll/CullPrepareGuard.cpp
        src/dll/ResourceSetWatch.cpp
        src/dll/FrameProbe.cpp
        src/dll/FocusHold.cpp
        src/dll/LoadingScreen.cpp
        src/dll/ControllerPrompts.cpp
        src/dll/DebugWatch.cpp
        src/dll/DllMain.cpp
        src/dll/FrameCaptureWin32.cpp
        src/dll/FrameObserverHook.cpp
        src/dll/FrameSchedule.cpp
        src/dll/HeadTrackingHook.cpp
        src/dll/HudBridge.cpp
        src/dll/ResearchHotkeys.cpp
        src/dll/IkTargetProbe.cpp
        src/dll/InputPath.cpp
        src/dll/InputPost.cpp
        src/dll/MinHookInit.cpp
        src/dll/MoveLane.cpp
        src/dll/StickReread.cpp
        src/dll/CommandChannel.cpp
        src/dll/WorkerThread.cpp
        src/dll/NearFovOverride.cpp
        src/dll/NearViewStereo.cpp
        src/dll/NearParticleStereo.cpp
        src/dll/MarkerStereo.cpp
        src/dll/StarfieldStereo.cpp
        src/dll/DualSceneStereo.cpp
        src/dll/DualOverlay.cpp
        src/dll/WeaponAttachment.cpp
        src/dll/PassCreationHook.cpp
        src/dll/ReticleFollow.cpp
        src/dll/XrInput.cpp
        src/dll/Haptics.cpp
        src/dll/SceneQuery.cpp
        src/dll/PhysicalInteractions.cpp
        src/dll/XrSessionHost.cpp
        src/dll/XrSessionLifecycle.cpp
        src/dll/XrEyePair.cpp
        src/dll/XrMenuPanel.cpp
        src/dll/XrOverlayLayers.cpp
        src/dll/XrHoldFrames.cpp
        src/dll/XrSessionReports.cpp
        src/lifecycle/Logger.cpp
    )
    target_include_directories(preyvr_dll_objects PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/dll"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/lifecycle"
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
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
        # **A PDB for every build** (docs/CRASHES-2026-10-24.md): without one a
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

    add_executable(preyvr_ray_probe tools/ray_probe/main.cpp)
    target_link_libraries(preyvr_ray_probe PRIVATE preyvr_core)
    preyvr_enable_warnings(preyvr_ray_probe)

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

    preyvr_add_test(vr_math tests/VrMathTests.cpp)
    preyvr_add_test(engine_map tests/EngineMapTests.cpp)
    preyvr_add_test(aim_state_fixture tests/AimStateTests.cpp TARGET preyvr_aim_state_tests)
    preyvr_add_test(wrench_query_fixture tests/WrenchQueryTests.cpp TARGET preyvr_wrench_query_tests)
    preyvr_add_test(interaction_query_fixture tests/InteractionQueryTests.cpp TARGET preyvr_interaction_query_tests)
    preyvr_add_test(runtime_snapshot tests/RuntimeSnapshotTests.cpp)
    preyvr_add_test(xr_swapchain_format tests/XrSwapchainFormatTests.cpp)
    preyvr_add_test(xr_frame_contract tests/XrFrameContractTests.cpp)
    preyvr_add_test(stereo_frame tests/StereoFrameTests.cpp)
    preyvr_add_test(comfort_controls tests/ComfortControlsTests.cpp)
    preyvr_add_test(motion_controller tests/MotionControllerTests.cpp)
    preyvr_add_test(two_handed_aim tests/TwoHandedAimTests.cpp)
    preyvr_add_test(vr_options_contracts tests/VrOptionsTests.cpp TARGET preyvr_vr_options_tests)
    preyvr_add_test(controller_profiles tests/ControllerProfilesTests.cpp)
    preyvr_add_test(action_map_walk tests/ActionMapWalkTests.cpp)
    preyvr_add_test(release_gate tests/ReleaseGateTests.cpp)
    preyvr_add_test(body_equipment_contracts tests/BodyEquipmentTests.cpp TARGET preyvr_body_equipment_tests)
    preyvr_add_test(anim_ik tests/AnimIkTests.cpp)
    preyvr_add_test(hand_pose tests/HandPoseTests.cpp)
    preyvr_add_test(arm_pose tests/ArmPoseTests.cpp)
    preyvr_add_test(body_turn tests/BodyTurnTests.cpp)
    preyvr_add_test(debug_draw tests/DebugDrawTests.cpp)
    preyvr_add_test(shot_ray tests/ShotRayTests.cpp)
    preyvr_add_test(near_particles tests/NearParticlesTests.cpp)
    preyvr_add_test(marker_depth tests/MarkerDepthTests.cpp)
    preyvr_add_test(starfield_depth tests/StarfieldDepthTests.cpp)
    preyvr_add_test(hand_carry tests/HandCarryTests.cpp)
    preyvr_add_test(grenade_throw tests/GrenadeThrowTests.cpp)
    preyvr_add_test(wrist_holo tests/WristHoloTests.cpp)
    preyvr_add_test(interaction_use tests/InteractionUseTests.cpp)
    preyvr_add_test(weapon_rig_alignment tests/WeaponRigAlignmentTests.cpp)
    preyvr_add_test(weapon_aim tests/WeaponAimTests.cpp)
    preyvr_add_test(scene_query_contracts tests/SceneQueryTests.cpp TARGET preyvr_scene_query_tests)
    preyvr_add_test(physical_interactions tests/PhysicalInteractionsTests.cpp)
    preyvr_add_test(contact_native tests/ContactNativeTests.cpp)
    preyvr_add_test(swing_native tests/SwingNativeTests.cpp)
    preyvr_add_test(swing_strike tests/SwingStrikeTests.cpp)
    preyvr_add_test(wrench_draw_native tests/WrenchDrawNativeTests.cpp)
    preyvr_add_test(contact_query tests/ContactQueryTests.cpp)
    preyvr_add_test(surface_stop tests/SurfaceStopTests.cpp)
    preyvr_add_test(psi_medkit_contracts tests/PsiMedkitTests.cpp TARGET preyvr_psi_medkit_tests)
    preyvr_add_test(frustum_coverage tests/FrustumCoverageTests.cpp)
    preyvr_add_test(frame_timing tests/FrameTimingTests.cpp)
    preyvr_add_test(input_event tests/InputEventTests.cpp)
    preyvr_add_test(input_queue tests/InputQueueTests.cpp)
    preyvr_add_test(menu_navigator tests/MenuNavigatorTests.cpp)
    preyvr_add_test(menu_hold tests/MenuHoldTests.cpp)
    preyvr_add_test(render_contract tests/RenderContractTests.cpp)
    preyvr_add_test(inventory_depth tests/InventoryDepthTests.cpp)
    preyvr_add_test(ui_panel tests/UiPanelTests.cpp)
    preyvr_add_test(position_ring tests/PositionRingTests.cpp)
    preyvr_add_test(xr_hold tests/XrHoldTests.cpp)
    preyvr_add_test(hud_parts tests/HudPartsTests.cpp)
    preyvr_add_test(locomotion tests/LocomotionTests.cpp)
    preyvr_add_test(stereo_camera tests/StereoCameraTests.cpp)
    preyvr_add_test(camera_edit tests/CameraEditTests.cpp)
    preyvr_add_test(frame_dump tests/FrameDumpTests.cpp)
    preyvr_add_test(debug_registers tests/DebugRegistersTests.cpp)
    preyvr_add_test(console_policy tests/ConsolePolicyTests.cpp)
    preyvr_add_test(runtime_bootstrap tests/RuntimeBootstrapTests.cpp)

    if(PREYVR_BUILD_SMOKE_DLL AND WIN32)
        preyvr_add_test(slot_feedback tests/SlotFeedbackTests.cpp
            DLL
            LIBS preyvr_core gdi32 user32)
        preyvr_add_test(psi_medkit_runtime tests/PsiMedkitRuntimeTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(physical_runtime tests/PhysicalRuntimeTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(contact_lane tests/ContactLaneTests.cpp
            DLL
            LIBS preyvr_core)
        preyvr_add_test(surface_stop_lane tests/SurfaceStopLaneTests.cpp
            DLL
            LIBS preyvr_core)
        preyvr_add_test(hand_world tests/HandWorldTests.cpp
            DLL
            LIBS preyvr_core)
        preyvr_add_test(swing_lane tests/SwingLaneTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(wrench_draw tests/WrenchDrawTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(wrench_sound tests/WrenchSoundTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(starfield_stereo tests/StarfieldStereoTests.cpp
            DLL
            LIBS preyvr_core minhook)
        preyvr_add_test(ui_pointer_dispatch tests/UiPointerTests.cpp TARGET preyvr_ui_pointer_tests
            DLL)
        # The VR Options panel, drawn by the real ImGui backend on WARP.
        preyvr_add_test(vr_options_ui tests/VrOptionsUiTests.cpp SOURCES src/dll/VrOptionsUi.cpp
            INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/dll"
            DLL
            LIBS preyvr_core preyvr_imgui d3d11)
        preyvr_add_test(native_wrist tests/NativeWristTests.cpp SOURCES src/dll/NativeWristTexture.cpp
            DLL
            LIBS preyvr_core d3d11 d3dcompiler)
        preyvr_add_test(vr_options_runtime tests/VrOptionsRuntimeTests.cpp
            DLL)

        preyvr_add_test(inventory_pointer_capture tests/InventoryPointerCaptureTests.cpp
            TARGET preyvr_inventory_capture_tests
            DLL
            LIBS minhook)
        preyvr_add_test(near_fov_override tests/NearFovOverrideTests.cpp TARGET preyvr_near_fov_tests
            DLL
            LIBS minhook)
        preyvr_add_test(cull_camera_watch tests/CullCameraWatchTests.cpp
            DLL
            LIBS minhook)
        preyvr_add_test(frame_probe tests/FrameProbeTests.cpp
            DLL
            LIBS minhook)
        preyvr_add_test(cull_prepare_guard tests/CullPrepareGuardTests.cpp
            DLL
            LIBS minhook)
        preyvr_add_test(resource_set_watch tests/ResourceSetWatchTests.cpp
            DLL
            LIBS minhook)
        preyvr_add_test(focus_hold tests/FocusHoldTests.cpp
            DLL
            LIBS preyvr_headers minhook user32)
        preyvr_add_test(controller_prompts tests/ControllerPromptsTests.cpp
            DLL)
        preyvr_add_test(hook_install tests/HookInstallTests.cpp
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
            preyvr_add_test(haptics_contracts tests/HapticsTests.cpp TARGET preyvr_haptics_tests
                SOURCES src/dll/Haptics.cpp
                INCLUDES $<TARGET_PROPERTY:openxr_loader,INTERFACE_INCLUDE_DIRECTORIES>
                DLL)
            preyvr_add_test(layer_swapchain tests/LayerSwapchainTests.cpp
                SOURCES src/dll/LayerSwapchain.cpp
                INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/dll"
                    $<TARGET_PROPERTY:openxr_loader,INTERFACE_INCLUDE_DIRECTORIES>
                DLL
                DEFS XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
                LIBS preyvr_headers d3d11)
            # The command channel's replies, pinned. The test includes the channel
            # and links the DLL's other objects but DllMain, with the logger and
            # MinHook's initialiser stubbed. Built apart from the copied loader, so
            # a stray OpenXR call fails to delay-load instead of finding a runtime.
            preyvr_add_test(command_channel tests/CommandChannelTests.cpp
                SOURCES "$<FILTER:$<TARGET_OBJECTS:preyvr_dll_objects>,EXCLUDE,/(CommandChannel|DllMain|Logger|MinHookInit)[.]obj$>"
                INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/dll" "${CMAKE_CURRENT_BINARY_DIR}/generated"
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
            preyvr_add_test(vr_options_wiring tests/VrOptionsWiringTests.cpp
                SOURCES "$<FILTER:$<TARGET_OBJECTS:preyvr_dll_objects>,EXCLUDE,/(VrOptionsRuntime|DllMain|Logger|MinHookInit)[.]obj$>"
                INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/dll" "${CMAKE_CURRENT_BINARY_DIR}/generated"
                DLL
                DEFS XR_USE_PLATFORM_WIN32 XR_USE_GRAPHICS_API_D3D11
                LIBS preyvr_core preyvr_win32_bootstrap preyvr_imgui minhook bcrypt psapi shell32 d3d11 dxgi
                    d3dcompiler gdi32 openxr_loader delayimp)
            target_link_options(preyvr_vr_options_wiring_tests PRIVATE /DELAYLOAD:openxr_loader.dll)
            set_target_properties(preyvr_vr_options_wiring_tests PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/command_channel")

            preyvr_add_test(openxr_preflight_files tests/OpenXRPreflightWin32Tests.cpp
                TARGET preyvr_openxr_preflight_tests
                INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/dll"
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
        foreach(suite Resolution Settings Stereo)
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
