// SPDX-License-Identifier: MIT
// FeatureManifestGenerator.cs —— 集中式 partial Feature manifest 源生成器

using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Baize.Ecs.Generator;

[Generator(LanguageNames.CSharp)]
public sealed class FeatureManifestGenerator : IIncrementalGenerator
{
    private const string FeatureAttributeMetadataName = "Baize.Ecs.EcsFeatureAttribute";
    private const string AddSystemAttributeMetadataName = "Baize.Ecs.AddSystemAttribute`1";
    private const string AddFeatureAttributeMetadataName = "Baize.Ecs.AddFeatureAttribute`1";
    private const string FeatureInterfaceMetadataName = "Baize.Ecs.IEcsFeature";
    private const string WorldMetadataName = "Baize.Ecs.EcsWorld";
    private const string PhaseMetadataName = "Baize.Ecs.Phase";
    private const string Category = "Baize.Ecs.Generator";

    private static readonly DiagnosticDescriptor InvalidFeatureDeclaration = new(
        "BAIZEECSGEN001",
        "Feature 声明不受支持",
        "Feature '{0}' 必须是非 static、非 generic、单一源码声明的 partial class；包含类型也必须是非 generic partial class",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor MissingFeatureInterface = new(
        "BAIZEECSGEN002",
        "Feature 未实现 IEcsFeature",
        "Feature '{0}' 必须实现 global::Baize.Ecs.IEcsFeature",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor ExistingInstallMethod = new(
        "BAIZEECSGEN003",
        "Feature 已有 Install 方法",
        "Feature '{0}' 已声明 Install；源生成器只做 additive generation，请删除手写 Install 后再迁移",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor InvalidSystemType = new(
        "BAIZEECSGEN004",
        "System 类型不受支持",
        "Feature '{0}' 声明的 System '{1}' 必须是非 abstract、闭合且继承 Baize.Ecs.EcsSystem 家族的 class",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor InaccessibleSystemType = new(
        "BAIZEECSGEN005",
        "System 类型不可访问",
        "System '{0}' 不可从 Feature '{1}' 的生成代码访问",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor MissingSystemConstructor = new(
        "BAIZEECSGEN006",
        "System 缺少可访问的无参构造器",
        "System '{0}' 必须提供可从 Feature '{1}' 访问的无参构造器",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor InvalidPhase = new(
        "BAIZEECSGEN007",
        "Phase 值无效",
        "Feature '{0}' 为 System '{1}' 声明的 Phase 值 '{2}' 不是已定义的 Baize.Ecs.Phase 成员",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor InvalidChildFeature = new(
        "BAIZEECSGEN008",
        "子 Feature 类型不受支持",
        "Feature '{0}' 声明的子 Feature '{1}' 必须是非 abstract、闭合、实现 IEcsFeature 的 class",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor InaccessibleChildFeature = new(
        "BAIZEECSGEN009",
        "子 Feature 类型不可访问",
        "子 Feature '{0}' 不可从 Feature '{1}' 的生成代码访问",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor MissingChildFeatureConstructor = new(
        "BAIZEECSGEN010",
        "子 Feature 缺少可访问的无参构造器",
        "子 Feature '{0}' 必须提供可从 Feature '{1}' 访问的无参构造器",
        Category,
        DiagnosticSeverity.Error,
        isEnabledByDefault: true);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var candidates = context.SyntaxProvider
            .ForAttributeWithMetadataName(
                FeatureAttributeMetadataName,
                static (node, _) => node is TypeDeclarationSyntax,
                static (attributeContext, _) => new FeatureCandidate(
                    (INamedTypeSymbol)attributeContext.TargetSymbol,
                    (TypeDeclarationSyntax)attributeContext.TargetNode))
            .Collect();

        context.RegisterSourceOutput(
            context.CompilationProvider.Combine(candidates),
            static (sourceContext, input) => Execute(sourceContext, input.Left, input.Right));
    }

    private static void Execute(
        SourceProductionContext context,
        Compilation compilation,
        ImmutableArray<FeatureCandidate> candidates)
    {
        INamedTypeSymbol? featureInterface = compilation.GetTypeByMetadataName(FeatureInterfaceMetadataName);
        INamedTypeSymbol? worldType = compilation.GetTypeByMetadataName(WorldMetadataName);
        INamedTypeSymbol? phaseType = compilation.GetTypeByMetadataName(PhaseMetadataName);
        ImmutableArray<INamedTypeSymbol> systemBases = GetSystemBases(compilation);

        foreach (FeatureCandidate candidate in candidates)
        {
            GenerateFeature(context, compilation, candidate, featureInterface, worldType, phaseType, systemBases);
        }
    }

    private static void GenerateFeature(
        SourceProductionContext context,
        Compilation compilation,
        FeatureCandidate candidate,
        INamedTypeSymbol? featureInterface,
        INamedTypeSymbol? worldType,
        INamedTypeSymbol? phaseType,
        ImmutableArray<INamedTypeSymbol> systemBases)
    {
        INamedTypeSymbol feature = candidate.Symbol;
        string featureName = feature.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        Location featureLocation = candidate.Declaration.Identifier.GetLocation();
        bool hasError = false;

        if (!IsValidFeatureDeclaration(feature, candidate.Declaration))
        {
            context.ReportDiagnostic(Diagnostic.Create(InvalidFeatureDeclaration, featureLocation, featureName));
            hasError = true;
        }

        if (featureInterface is null || !Implements(feature, featureInterface))
        {
            context.ReportDiagnostic(Diagnostic.Create(MissingFeatureInterface, featureLocation, featureName));
            hasError = true;
        }

        IMethodSymbol? existingInstall = feature.GetMembers("Install").OfType<IMethodSymbol>()
            .FirstOrDefault(method => !method.IsImplicitlyDeclared);
        if (existingInstall is not null)
        {
            context.ReportDiagnostic(Diagnostic.Create(
                ExistingInstallMethod,
                existingInstall.Locations.FirstOrDefault() ?? featureLocation,
                featureName));
            hasError = true;
        }

        if (worldType is null || phaseType is null || systemBases.IsDefaultOrEmpty)
        {
            return;
        }

        var actions = new List<ManifestAction>();
        IEnumerable<AttributeData> manifestAttributes = feature.GetAttributes()
            .Where(IsManifestAttribute)
            .OrderBy(attribute => attribute.ApplicationSyntaxReference?.Span.Start ?? int.MaxValue);

        foreach (AttributeData attribute in manifestAttributes)
        {
            Location location = attribute.ApplicationSyntaxReference?.GetSyntax().GetLocation() ?? featureLocation;
            INamedTypeSymbol? attributeClass = attribute.AttributeClass;
            if (attributeClass is null || attributeClass.TypeArguments.Length != 1)
            {
                continue;
            }

            ITypeSymbol declaredType = attributeClass.TypeArguments[0];
            string attributeMetadataName = GetMetadataName(attributeClass.OriginalDefinition);
            if (attributeMetadataName == AddSystemAttributeMetadataName)
            {
                if (declaredType is not INamedTypeSymbol systemType ||
                    !IsValidSystem(systemType, systemBases))
                {
                    context.ReportDiagnostic(Diagnostic.Create(
                        InvalidSystemType,
                        location,
                        featureName,
                        Display(declaredType)));
                    hasError = true;
                    continue;
                }

                if (!compilation.IsSymbolAccessibleWithin(systemType, feature))
                {
                    context.ReportDiagnostic(Diagnostic.Create(
                        InaccessibleSystemType,
                        location,
                        Display(systemType),
                        featureName));
                    hasError = true;
                    continue;
                }

                if (!HasAccessibleParameterlessConstructor(compilation, systemType, feature))
                {
                    context.ReportDiagnostic(Diagnostic.Create(
                        MissingSystemConstructor,
                        location,
                        Display(systemType),
                        featureName));
                    hasError = true;
                    continue;
                }

                TypedConstant phaseArgument = attribute.ConstructorArguments.Length == 1
                    ? attribute.ConstructorArguments[0]
                    : default;
                string? phaseName = GetPhaseName(phaseType, phaseArgument);
                if (phaseName is null)
                {
                    context.ReportDiagnostic(Diagnostic.Create(
                        InvalidPhase,
                        location,
                        featureName,
                        Display(systemType),
                        phaseArgument.Value?.ToString() ?? "<missing>"));
                    hasError = true;
                    continue;
                }

                actions.Add(ManifestAction.System(systemType, phaseName));
                continue;
            }

            if (attributeMetadataName == AddFeatureAttributeMetadataName)
            {
                if (declaredType is not INamedTypeSymbol childFeature ||
                    !IsValidChildFeature(childFeature, featureInterface))
                {
                    context.ReportDiagnostic(Diagnostic.Create(
                        InvalidChildFeature,
                        location,
                        featureName,
                        Display(declaredType)));
                    hasError = true;
                    continue;
                }

                if (!compilation.IsSymbolAccessibleWithin(childFeature, feature))
                {
                    context.ReportDiagnostic(Diagnostic.Create(
                        InaccessibleChildFeature,
                        location,
                        Display(childFeature),
                        featureName));
                    hasError = true;
                    continue;
                }

                if (!HasAccessibleParameterlessConstructor(compilation, childFeature, feature))
                {
                    context.ReportDiagnostic(Diagnostic.Create(
                        MissingChildFeatureConstructor,
                        location,
                        Display(childFeature),
                        featureName));
                    hasError = true;
                    continue;
                }

                actions.Add(ManifestAction.Feature(childFeature));
            }
        }

        if (hasError)
        {
            return;
        }

        string source = Render(feature, actions);
        context.AddSource(CreateHintName(feature), SourceText.From(source, Encoding.UTF8));
    }

    private static bool IsManifestAttribute(AttributeData attribute)
    {
        string metadataName = GetMetadataName(attribute.AttributeClass?.OriginalDefinition);
        return metadataName == AddSystemAttributeMetadataName || metadataName == AddFeatureAttributeMetadataName;
    }

    private static bool IsValidFeatureDeclaration(INamedTypeSymbol feature, TypeDeclarationSyntax declaration)
    {
        if (feature.TypeKind != TypeKind.Class || declaration is not ClassDeclarationSyntax || feature.IsStatic)
        {
            return false;
        }
        if (feature.DeclaringSyntaxReferences.Length != 1 || feature.Arity != 0)
        {
            return false;
        }
        if (!declaration.Modifiers.Any(SyntaxKind.PartialKeyword))
        {
            return false;
        }

        for (INamedTypeSymbol? containing = feature.ContainingType; containing is not null; containing = containing.ContainingType)
        {
            if (containing.TypeKind != TypeKind.Class || containing.Arity != 0)
            {
                return false;
            }
            foreach (SyntaxReference syntaxReference in containing.DeclaringSyntaxReferences)
            {
                if (syntaxReference.GetSyntax() is not ClassDeclarationSyntax containingDeclaration ||
                    !containingDeclaration.Modifiers.Any(SyntaxKind.PartialKeyword))
                {
                    return false;
                }
            }
        }
        return true;
    }

    private static ImmutableArray<INamedTypeSymbol> GetSystemBases(Compilation compilation)
    {
        var builder = ImmutableArray.CreateBuilder<INamedTypeSymbol>();
        for (int arity = 0; arity <= 5; arity++)
        {
            string metadataName = arity == 0 ? "Baize.Ecs.EcsSystem" : $"Baize.Ecs.EcsSystem`{arity}";
            INamedTypeSymbol? symbol = compilation.GetTypeByMetadataName(metadataName);
            if (symbol is not null)
            {
                builder.Add(symbol);
            }
        }
        return builder.ToImmutable();
    }

    private static bool IsValidSystem(INamedTypeSymbol system, ImmutableArray<INamedTypeSymbol> systemBases)
    {
        if (system.TypeKind != TypeKind.Class || system.IsAbstract || system.IsStatic || !IsClosedType(system))
        {
            return false;
        }

        for (INamedTypeSymbol? current = system; current is not null; current = current.BaseType)
        {
            if (systemBases.Any(baseType =>
                    SymbolEqualityComparer.Default.Equals(current.OriginalDefinition, baseType)))
            {
                return true;
            }
        }
        return false;
    }

    private static bool IsValidChildFeature(INamedTypeSymbol feature, INamedTypeSymbol? featureInterface)
    {
        if (featureInterface is null)
        {
            return false;
        }
        return feature.TypeKind == TypeKind.Class &&
            !feature.IsAbstract &&
            !feature.IsStatic &&
            IsClosedType(feature) &&
            Implements(feature, featureInterface);
    }
    private static bool Implements(INamedTypeSymbol type, INamedTypeSymbol expectedInterface) =>
        type.AllInterfaces.Any(candidate => SymbolEqualityComparer.Default.Equals(candidate, expectedInterface));

    private static bool IsClosedType(INamedTypeSymbol type)
    {
        for (INamedTypeSymbol? current = type; current is not null; current = current.ContainingType)
        {
            foreach (ITypeSymbol argument in current.TypeArguments)
            {
                if (ContainsTypeParameter(argument))
                {
                    return false;
                }
            }
        }
        return true;
    }

    private static bool ContainsTypeParameter(ITypeSymbol type)
    {
        if (type.TypeKind == TypeKind.TypeParameter)
        {
            return true;
        }
        if (type is IArrayTypeSymbol array)
        {
            return ContainsTypeParameter(array.ElementType);
        }
        if (type is IPointerTypeSymbol pointer)
        {
            return ContainsTypeParameter(pointer.PointedAtType);
        }
        if (type is INamedTypeSymbol named)
        {
            return named.TypeArguments.Any(ContainsTypeParameter);
        }
        return false;
    }
    private static bool HasAccessibleParameterlessConstructor(
        Compilation compilation,
        INamedTypeSymbol type,
        INamedTypeSymbol within) =>
        type.InstanceConstructors.Any(constructor =>
            constructor.Parameters.Length == 0 && compilation.IsSymbolAccessibleWithin(constructor, within));

    private static string? GetPhaseName(INamedTypeSymbol phaseType, TypedConstant argument)
    {
        if (argument.Kind != TypedConstantKind.Enum || argument.Value is null)
        {
            return null;
        }

        long value;
        try
        {
            value = Convert.ToInt64(argument.Value, CultureInfo.InvariantCulture);
        }
        catch (Exception)
        {
            return null;
        }

        foreach (IFieldSymbol field in phaseType.GetMembers().OfType<IFieldSymbol>())
        {
            if (!field.HasConstantValue || field.ConstantValue is null)
            {
                continue;
            }
            if (Convert.ToInt64(field.ConstantValue, CultureInfo.InvariantCulture) == value)
            {
                return field.Name;
            }
        }
        return null;
    }

    private static string Render(INamedTypeSymbol feature, IReadOnlyList<ManifestAction> actions)
    {
        var builder = new StringBuilder();
        builder.AppendLine("// <auto-generated/>");
        builder.AppendLine("// SPDX-License-Identifier: MIT");
        builder.AppendLine("#nullable enable");

        int indent = 0;
        if (!feature.ContainingNamespace.IsGlobalNamespace)
        {
            builder.Append("namespace ").Append(feature.ContainingNamespace.ToDisplayString()).AppendLine();
            builder.AppendLine("{");
            indent++;
        }

        var hierarchy = new Stack<INamedTypeSymbol>();
        for (INamedTypeSymbol? current = feature; current is not null; current = current.ContainingType)
        {
            hierarchy.Push(current);
        }

        while (hierarchy.Count > 0)
        {
            INamedTypeSymbol current = hierarchy.Pop();
            AppendIndent(builder, indent);
            builder.Append(GetTypeDeclaration(current)).AppendLine();
            AppendIndent(builder, indent);
            builder.AppendLine("{");
            indent++;
        }

        AppendIndent(builder, indent);
        builder.AppendLine("/// <summary>按集中式 Feature manifest 的词法顺序安装。</summary>");
        AppendIndent(builder, indent);
        builder.AppendLine("public void Install(global::Baize.Ecs.EcsWorld world)");
        AppendIndent(builder, indent);
        builder.AppendLine("{");
        indent++;

        for (int index = 0; index < actions.Count; index++)
        {
            ManifestAction action = actions[index];
            AppendIndent(builder, indent);
            if (action.Kind == ManifestActionKind.System)
            {
                builder.Append("// ").Append(index + 1).Append(". System: ")
                    .Append(Display(action.Type)).Append(" | Phase: ").Append(action.PhaseName).AppendLine();
                AppendIndent(builder, indent);
                builder.Append("world.AddSystem(new ").Append(Display(action.Type)).Append("(), global::Baize.Ecs.Phase.")
                    .Append(action.PhaseName).AppendLine(");");
            }
            else
            {
                builder.Append("// ").Append(index + 1).Append(". 子 Feature: ")
                    .Append(Display(action.Type)).AppendLine("（立即安装）");
                AppendIndent(builder, indent);
                builder.Append("world.AddFeature(new ").Append(Display(action.Type)).AppendLine("());");
            }
        }

        indent--;
        AppendIndent(builder, indent);
        builder.AppendLine("}");

        int closeCount = 1;
        for (INamedTypeSymbol? current = feature.ContainingType; current is not null; current = current.ContainingType)
        {
            closeCount++;
        }
        for (int index = 0; index < closeCount; index++)
        {
            indent--;
            AppendIndent(builder, indent);
            builder.AppendLine("}");
        }

        if (!feature.ContainingNamespace.IsGlobalNamespace)
        {
            indent--;
            AppendIndent(builder, indent);
            builder.AppendLine("}");
        }
        return builder.ToString();
    }

    private static string GetTypeDeclaration(INamedTypeSymbol type)
    {
        var parts = new List<string>();
        string accessibility = GetAccessibility(type.DeclaredAccessibility);
        if (accessibility.Length != 0)
        {
            parts.Add(accessibility);
        }
        if (type.IsStatic)
        {
            parts.Add("static");
        }
        else
        {
            if (type.IsAbstract)
            {
                parts.Add("abstract");
            }
            if (type.IsSealed)
            {
                parts.Add("sealed");
            }
        }
        parts.Add("partial");
        parts.Add("class");
        parts.Add(EscapeIdentifier(type.Name));
        return string.Join(" ", parts);
    }

    private static string GetAccessibility(Accessibility accessibility)
    {
        switch (accessibility)
        {
            case Accessibility.Public:
                return "public";
            case Accessibility.Internal:
                return "internal";
            case Accessibility.Private:
                return "private";
            case Accessibility.Protected:
                return "protected";
            case Accessibility.ProtectedAndInternal:
                return "private protected";
            case Accessibility.ProtectedOrInternal:
                return "protected internal";
            default:
                return string.Empty;
        }
    }

    private static string EscapeIdentifier(string name) =>
        SyntaxFacts.GetKeywordKind(name) != SyntaxKind.None ? "@" + name : name;

    private static void AppendIndent(StringBuilder builder, int indent) => builder.Append(' ', indent * 4);

    private static string Display(ITypeSymbol type) =>
        type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);

    private static string GetMetadataName(INamedTypeSymbol? type)
    {
        if (type is null)
        {
            return string.Empty;
        }
        string prefix = type.ContainingNamespace.IsGlobalNamespace
            ? string.Empty
            : type.ContainingNamespace.ToDisplayString() + ".";
        return prefix + type.MetadataName;
    }

    private static string CreateHintName(INamedTypeSymbol feature)
    {
        string identity = feature.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        var safeName = new StringBuilder(identity.Length);
        foreach (char character in identity)
        {
            safeName.Append(char.IsLetterOrDigit(character) ? character : '_');
        }
        return $"{safeName}_{Fnv1a(identity):X8}.FeatureManifest.g.cs";
    }

    private static uint Fnv1a(string value)
    {
        uint hash = 2166136261;
        foreach (char character in value)
        {
            hash ^= character;
            hash *= 16777619;
        }
        return hash;
    }

    private sealed class FeatureCandidate
    {
        public FeatureCandidate(INamedTypeSymbol symbol, TypeDeclarationSyntax declaration)
        {
            Symbol = symbol;
            Declaration = declaration;
        }

        public INamedTypeSymbol Symbol { get; }
        public TypeDeclarationSyntax Declaration { get; }
    }

    private enum ManifestActionKind
    {
        System,
        Feature,
    }

    private sealed class ManifestAction
    {
        private ManifestAction(ManifestActionKind kind, INamedTypeSymbol type, string? phaseName)
        {
            Kind = kind;
            Type = type;
            PhaseName = phaseName;
        }

        public ManifestActionKind Kind { get; }
        public INamedTypeSymbol Type { get; }
        public string? PhaseName { get; }

        public static ManifestAction System(INamedTypeSymbol type, string phaseName) =>
            new(ManifestActionKind.System, type, phaseName);

        public static ManifestAction Feature(INamedTypeSymbol type) =>
            new(ManifestActionKind.Feature, type, null);
    }
}
