// The installer owns program files and OS integration. User projects, imported
// plugins and settings live outside TargetDir and are never install operations.
function Component()
{
    if (!installer.containsValue("AllUsers")) installer.setValue("AllUsers", "false");
    if (installer.isInstaller() && !installer.isCommandLineInstance()) {
        installer.addWizardPage(component, "InstallScope", QInstaller.TargetDirectory);
        var page = component.userInterface("InstallScope");
        page.CurrentUser.checked = installer.value("AllUsers") !== "true";
        page.AllUsers.checked = installer.value("AllUsers") === "true";
        page.AllUsers.toggled.connect(function(allUsers) {
            installer.setValue("AllUsers", allUsers ? "true" : "false");
            var base = allUsers ? installer.value("ApplicationsDirX64") : installer.environmentVariable("LOCALAPPDATA") + "/Programs";
            installer.setValue("TargetDir", base + "/CoRig");
        });
        if (installer.value("AllUsers") !== "true")
            installer.setValue("TargetDir", installer.environmentVariable("LOCALAPPDATA") + "/Programs/CoRig");
    }
}

Component.prototype.createOperations = function()
{
    if (installer.value("AllUsers") === "true" && !installer.gainAdminRights())
        throw new Error("安装到所有用户需要管理员权限。");
    component.createOperations();
    if (systemInfo.productType !== "windows") return;
    var menu = installer.value("AllUsers") === "true" ? "@AllUsersStartMenuProgramsPath@/CoRig" : "@UserStartMenuProgramsPath@/CoRig";
    if (installer.value("CreateShortcuts", "true") === "true") {
        component.addOperation("CreateShortcut", "@TargetDir@/CoRig.exe", menu + "/联调 CoRig.lnk", "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/CoRig.exe");
        component.addOperation("CreateShortcut", "@TargetDir@/CoRigMaintenanceTool.exe", menu + "/更新与卸载 CoRig.lnk", "workingDirectory=@TargetDir@");
    }
    if (installer.value("RegisterFileTypes", "true") === "true") {
        component.addOperation("RegisterFileType", "crpj", '"@TargetDir@/CoRig.exe" "%1"', "CoRig 工程", "application/x-corig-project", "@TargetDir@/CoRig.exe,0", "ProgId=CoRig.Project");
        component.addOperation("RegisterFileType", "crpl", '"@TargetDir@/CoRig.exe" "%1"', "CoRig 插件包", "application/x-corig-plugin", "@TargetDir@/CoRig.exe,0", "ProgId=CoRig.Plugin");
    }
}
