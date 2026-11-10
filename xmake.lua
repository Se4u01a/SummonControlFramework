set_xmakever("3.0.8")

local commonlib_path = os.getenv("COMMONLIBSSE_NG_PATH")
if not commonlib_path or not os.exists(commonlib_path) then
    if os.exists("D:/Games/Issen/CommonLibSSE-NG") then
        commonlib_path = "D:/Games/Issen/CommonLibSSE-NG"
    elseif os.exists("extern/CommonLibSSE-NG") then
        commonlib_path = "extern/CommonLibSSE-NG"
    end
end

if not commonlib_path or not os.exists(commonlib_path) then
    raise("CommonLibSSE-NG not found! Please set COMMONLIBSSE_NG_PATH or clone into extern/CommonLibSSE-NG")
end

includes(commonlib_path)

set_project("SummonControlFramework")
set_version("0.1.0")

add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.compile_commands.autoupdate")
set_defaultplat("windows")
set_defaultarchs("windows|x64")
set_allowedplats("windows")
set_allowedarchs("x64")
set_defaultmode("releasedbg")
set_languages("c++latest")

set_warnings("allextra")

set_policy("build.fence", true)
set_policy("check.auto_ignore_flags", false)

-- Runtimes: SE + AE (Steam AE 1.6.1170 is primary target for PoC)
set_config("skyrim_vr", false)
set_config("skyrim_ae", true)
set_config("skyrim_se", true)
set_config("skse_xbyak", true)

target("SummonControlFramework")
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = "SummonControlFramework",
        author = "Sequoia",
        description = "Summon Control Framework for Skyrim SE/AE"
    })

    add_includedirs("src")
    set_encodings("utf-8")
    set_pcxxheader("src/PCH.h")
    add_headerfiles("src/**.h")
    add_files(
        "src/main.cpp",
        "src/Core/**.cpp",
        "src/AI/**.cpp",
        "src/Collision/**.cpp",
        "src/FriendlyFire/**.cpp",
        "src/Integrations/**.cpp",
        "src/Engine/**.cpp",
        "src/Plugin/**.cpp"
    )

    after_build(function (target)
        local deploy_dirs = {
            "D:/Games/stb/[STB] Mod Organizer/mods/Summon Control Framework/SKSE/Plugins",
            "D:/Games/stb/[RE2CE TEST] Mod Organizer/mods/Summon Control Framework/SKSE/Plugins"
        }
        for _, deploy_dir in ipairs(deploy_dirs) do
            os.mkdir(deploy_dir)
            if not os.trycp(target:targetfile(), deploy_dir) then
                cprint("${yellow}warning: %s is in use: not copied", path.join(deploy_dir, path.filename(target:targetfile())))
            else
                local pdb = path.join(path.directory(target:targetfile()), target:basename() .. ".pdb")
                if os.isfile(pdb) then
                    os.trycp(pdb, deploy_dir)
                end
                cprint("${green}deployed to %s", deploy_dir)
            end
        end

        import("core.base.task")
        task.run("project", {kind = "compile_commands"})
    end)

-- Engine-free Core unit tests (runs without Skyrim runtime)
target("scf_tests")
    set_kind("binary")
    set_default(true)
    set_languages("c++latest")
    set_encodings("utf-8")
    set_rundir("$(projectdir)")
    add_includedirs("src", "tests")
    add_headerfiles("src/Core/**.h", "src/AI/**.h", "src/Collision/**.h", "src/FriendlyFire/**.h", "tests/**.h")
    add_files("src/Core/**.cpp", "src/AI/**.cpp", "src/Collision/**.cpp", "src/FriendlyFire/RelationshipClassifier.cpp", "tests/**.cpp")
