using UnrealBuildTool;

public class RefereeCareerEditorTarget : TargetRules
{
	public RefereeCareerEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("RefereeCareer");
	}
}
