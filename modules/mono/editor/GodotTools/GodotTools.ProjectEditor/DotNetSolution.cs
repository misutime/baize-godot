using GodotTools.Core;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using Microsoft.VisualStudio.SolutionPersistence;
using Microsoft.VisualStudio.SolutionPersistence.Model;
using Microsoft.VisualStudio.SolutionPersistence.Serializer;

namespace GodotTools.ProjectEditor
{
    public class DotNetSolution
    {
        // FORK-CUSTOM（All-in C#）：默认生成 .slnx（.NET 生态新标准，XML 简洁、merge 友好）。
        // 用官方 SolutionPersistence（Microsoft.VisualStudio.SolutionPersistence）序列化——
        // 自动保证 schema 正确（BuildType/Platform/ProjectType 声明）与 XML 属性转义。
        // 保留 .sln 生成能力（GenerateSlnx=false 时兼容旧流程）。


        private const string _solutionTemplate =
@"Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio 2012
{0}
Global
	GlobalSection(SolutionConfigurationPlatforms) = preSolution
{1}
	EndGlobalSection
	GlobalSection(ProjectConfigurationPlatforms) = postSolution
{2}
	EndGlobalSection
EndGlobal
";

        private const string _projectDeclaration =
@"Project(""{{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}}"") = ""{0}"", ""{1}"", ""{{{2}}}""
EndProject";

        private const string _solutionPlatformsConfig =
@"	{0}|Any CPU = {0}|Any CPU";

        private const string _projectPlatformsConfig =
@"		{{{0}}}.{1}|Any CPU.ActiveCfg = {1}|Any CPU
		{{{0}}}.{1}|Any CPU.Build.0 = {1}|Any CPU";

        private readonly Dictionary<string, ProjectInfo> _projects = new Dictionary<string, ProjectInfo>();

        public string Name { get; }
        public string DirectoryPath { get; }

        public class ProjectInfo
        {
            public string Guid { get; }
            public string PathRelativeToSolution { get; }
            public List<string> Configs { get; }

            public ProjectInfo(string guid, string pathRelativeToSolution, List<string> configs)
            {
                Guid = guid;
                PathRelativeToSolution = pathRelativeToSolution;
                Configs = configs;
            }
        }

        // FORK-CUSTOM（All-in C#）：默认生成 .slnx（.NET 生态新标准）；设 false 回退 .sln。
        public bool GenerateSlnx { get; set; } = true;

        // slnx 的 Platform 声明（与上游 .sln 的 "Any CPU" 一致）。
        private readonly List<string> _platforms = new() { "Any CPU" };

        // FORK-CUSTOM（P1-3）：实际存在的旧解决方案完整路径（可能非默认命名，如 Tools.sln）。
        // 保存后据此精确清理另一格式，避免双文件（FindSolutionFileWithAssemblyName 多匹配视为错误）。
        public string? LegacySolutionPath { get; set; }

        public void AddNewProject(string name, ProjectInfo projectInfo)
        {
            _projects[name] = projectInfo;
        }

        public bool HasProject(string name)
        {
            return _projects.ContainsKey(name);
        }

        public ProjectInfo GetProjectInfo(string name)
        {
            return _projects[name];
        }

        public bool RemoveProject(string name)
        {
            return _projects.Remove(name);
        }

        public void Save()
        {
            if (!Directory.Exists(DirectoryPath))
                throw new FileNotFoundException("The solution directory does not exist.");

            // FORK-CUSTOM（All-in C#）：默认生成 .slnx（生态新标准）；GenerateSlnx=false 时生成 .sln。
            if (GenerateSlnx)
            {
                // 用官方 SolutionPersistence 模型构建——自动产出 schema 合法的 slnx
                // （BuildType/Platform 声明 + XML 属性转义）。
                var model = new SolutionModel();
                foreach (string platform in _platforms)
                {
                    model.AddPlatform(platform);
                }
                foreach (var pair in _projects)
                {
                    ProjectInfo projectInfo = pair.Value;
                    foreach (string config in projectInfo.Configs)
                    {
                        model.AddBuildType(config);
                    }
                    model.AddProject(projectInfo.PathRelativeToSolution.Replace("/", "\\", StringComparison.Ordinal), null, null);
                }

                string slnxPath = Path.Combine(DirectoryPath, Name + ".slnx");
                SolutionSerializers.SlnXml.SaveAsync(slnxPath, model, CancellationToken.None).GetAwaiter().GetResult();

                // FORK-CUSTOM（P1-3 修复）：避免新旧格式双文件——生成 slnx 后移除实际旧解决方案
                // （不假设文件名=Name；调用方传入 FindSolutionFileWithAssemblyName 检测到的精确路径）。
                if (!string.IsNullOrEmpty(LegacySolutionPath) && File.Exists(LegacySolutionPath) && LegacySolutionPath.EndsWith(".sln", StringComparison.OrdinalIgnoreCase))
                {
                    FileUtils.SaveBackupCopy(LegacySolutionPath);
                    File.Delete(LegacySolutionPath);
                }
                return;
            }

            string projectsDecl = string.Empty;
            string slnPlatformsCfg = string.Empty;
            string projPlatformsCfg = string.Empty;

            bool isFirstProject = true;

            foreach (var pair in _projects)
            {
                string name = pair.Key;
                ProjectInfo projectInfo = pair.Value;

                if (!isFirstProject)
                    projectsDecl += "\n";

                projectsDecl += string.Format(CultureInfo.InvariantCulture, _projectDeclaration,
                    name, projectInfo.PathRelativeToSolution.Replace("/", "\\", StringComparison.Ordinal), projectInfo.Guid);

                for (int i = 0; i < projectInfo.Configs.Count; i++)
                {
                    string config = projectInfo.Configs[i];

                    if (i != 0 || !isFirstProject)
                    {
                        slnPlatformsCfg += "\n";
                        projPlatformsCfg += "\n";
                    }

                    slnPlatformsCfg += string.Format(CultureInfo.InvariantCulture, _solutionPlatformsConfig, config);
                    projPlatformsCfg += string.Format(CultureInfo.InvariantCulture, _projectPlatformsConfig, projectInfo.Guid, config);
                }

                isFirstProject = false;
            }

            string solutionPath = Path.Combine(DirectoryPath, Name + ".sln");
            string content = string.Format(CultureInfo.InvariantCulture, _solutionTemplate, projectsDecl, slnPlatformsCfg, projPlatformsCfg);

            File.WriteAllText(solutionPath, content, Encoding.UTF8); // UTF-8 with BOM

            // FORK-CUSTOM（P1-3 修复，反向切换）：生成 .sln 后移除已有 .slnx（避免双文件）。
            if (!string.IsNullOrEmpty(LegacySolutionPath) && File.Exists(LegacySolutionPath) && LegacySolutionPath.EndsWith(".slnx", StringComparison.OrdinalIgnoreCase))
            {
                FileUtils.SaveBackupCopy(LegacySolutionPath);
                File.Delete(LegacySolutionPath);
            }
        }

        public DotNetSolution(string name, string directoryPath)
        {
            Name = name;
            DirectoryPath = directoryPath.IsAbsolutePath() ? directoryPath : Path.GetFullPath(directoryPath);
        }

        public static void MigrateFromOldConfigNames(string slnPath)
        {
            if (!File.Exists(slnPath))
                return;

            string input = File.ReadAllText(slnPath);

            if (!Regex.IsMatch(input, Regex.Escape("Tools|Any CPU")))
                return;

            // This method renames old configurations in solutions to the new ones.
            //
            // This is the order configs appear in the solution and what we want to rename them to:
            //   Debug|Any CPU = Debug|Any CPU        ->    ExportDebug|Any CPU = ExportDebug|Any CPU
            //   Tools|Any CPU = Tools|Any CPU        ->    Debug|Any CPU = Debug|Any CPU
            //
            // But we want to move Tools (now Debug) to the top, so it's easier to rename like this:
            //   Debug|Any CPU = Debug|Any CPU        ->    Debug|Any CPU = Debug|Any CPU
            //   Release|Any CPU = Release|Any CPU    ->    ExportDebug|Any CPU = ExportDebug|Any CPU
            //   Tools|Any CPU = Tools|Any CPU        ->    ExportRelease|Any CPU = ExportRelease|Any CPU

            var dict = new Dictionary<string, string>
            {
                {"Debug|Any CPU", "Debug|Any CPU"},
                {"Release|Any CPU", "ExportDebug|Any CPU"},
                {"Tools|Any CPU", "ExportRelease|Any CPU"}
            };

            var regex = new Regex(string.Join("|", dict.Keys.Select(Regex.Escape)));
            string result = regex.Replace(input, m => dict[m.Value]);

            if (result != input)
            {
                // Save a copy of the solution before replacing it
                FileUtils.SaveBackupCopy(slnPath);

                File.WriteAllText(slnPath, result);
            }
        }
    }
}
