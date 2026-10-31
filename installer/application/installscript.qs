// CoRig keeps program files, plugins and personal projects in one folder, so
// the installer only ever owns program files. The scope page pins the default
// target to a writable per-user location; the folder page may still select
// another writable disk.
function Component()
{
    if (installer.isInstaller() && !installer.isCommandLineInstance()) {
        installer.addWizardPage(component, "InstallScope", QInstaller.TargetDirectory);
        installer.setValue("TargetDir", installer.environmentVariable("LOCALAPPDATA") + "/Programs/CoRig");
    }
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (systemInfo.productType !== "windows") return;
    // Updating must never rewrite the folders that hold user content.
    component.addOperation("Mkdir", "@TargetDir@/project");
    component.addOperation("Mkdir", "@TargetDir@/plugins");
    var menu = "@UserStartMenuProgramsPath@/CoRig";
    if (installer.value("CreateShortcuts", "true") === "true") {
        component.addOperation("CreateShortcut", "@TargetDir@/CoRig.exe", menu + "/联调 CoRig.lnk", "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/CoRig.exe");
        component.addOperation("CreateShortcut", "@TargetDir@/CoRigMaintenanceTool.exe", menu + "/更新与卸载 CoRig.lnk", "workingDirectory=@TargetDir@");
    }
    if (installer.value("RegisterFileTypes", "true") === "true") {
        component.addOperation("RegisterFileType", "crpj", '"@TargetDir@/CoRig.exe" "%1"', "CoRig 工程", "application/x-corig-project", "@TargetDir@/CoRig.exe,0", "ProgId=CoRig.Project");
        component.addOperation("RegisterFileType", "crpl", '"@TargetDir@/CoRig.exe" "%1"', "CoRig 插件包", "application/x-corig-plugin", "@TargetDir@/CoRig.exe,0", "ProgId=CoRig.Plugin");
    }
}
