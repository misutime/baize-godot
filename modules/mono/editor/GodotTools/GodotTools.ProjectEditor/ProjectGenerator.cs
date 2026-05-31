using System;
using System.Globalization;
using System.IO;
using System.Text;
using Microsoft.Build.Construction;
using Microsoft.Build.Evaluation;
using GodotTools.Shared;

namespace GodotTools.ProjectEditor
{
    public static class ProjectGenerator
    {
        public static string GodotSdkAttrValue => $"Godot.NET.Sdk/{GeneratedGodotNupkgsVersions.GodotNETSdk}";

        // 定制版默认让用户项目直接使用 .NET 10；GodotSharp 自身仍保留上游的 net8.0 基线。
        public static string GodotMinimumRequiredTfm => "net10.0";

        public static ProjectRootElement GenGameProject(string name)
        {
            if (name.Length == 0)
                throw new ArgumentException("Project name is empty.", nameof(name));

            var root = ProjectRootElement.Create(NewProjectFileOptions.None);

            root.Sdk = GodotSdkAttrValue;

            var mainGroup = root.AddPropertyGroup();
            mainGroup.AddProperty("TargetFramework", GodotMinimumRequiredTfm);

            mainGroup.AddProperty("EnableDynamicLoading", "true");

            string sanitizedName = IdentifierUtils.SanitizeQualifiedIdentifier(name, allowEmptyIdentifiers: true);

            // If the name is not a valid namespace, manually set RootNamespace to a sanitized one.
            if (sanitizedName != name)
                mainGroup.AddProperty("RootNamespace", sanitizedName);

            return root;
        }

        public static void SaveNuGetConfig(string dir)
        {
            string toolsDir = Path.GetDirectoryName(typeof(ProjectGenerator).Assembly.Location) ?? string.Empty;
            string nupkgsDir = Path.Combine(toolsDir, "nupkgs");

            if (!Directory.Exists(nupkgsDir))
                return;

            string relativeNupkgsDir = Path.GetRelativePath(dir, nupkgsDir).Replace('\\', '/');
            string nugetConfigPath = Path.Combine(dir, "NuGet.config");

            // 定制版 SDK 和 GodotSharp 都是本地构建出来的包；写入项目级 NuGet 源，避免 VS Code 只去 nuget.org 找官方包。
            string content =
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" +
                "<configuration>\n" +
                "  <packageSources>\n" +
                "    <add key=\"Baize Godot Local\" value=\"" + relativeNupkgsDir + "\" />\n" +
                "    <add key=\"nuget.org\" value=\"https://api.nuget.org/v3/index.json\" />\n" +
                "  </packageSources>\n" +
                "</configuration>\n";

            File.WriteAllText(nugetConfigPath, content, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
        }

        public static string GenAndSaveGameProject(string dir, string name)
        {
            if (name.Length == 0)
                throw new ArgumentException("Project name is empty.", nameof(name));

            string path = Path.Combine(dir, name + ".csproj");

            var root = GenGameProject(name);

            // Save (without BOM)
            root.Save(path, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
            SaveNuGetConfig(dir);

            return Guid.NewGuid().ToString().ToUpperInvariant();
        }
    }
}
