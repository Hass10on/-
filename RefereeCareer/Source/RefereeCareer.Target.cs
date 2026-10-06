using UnrealBuildTool;

public class RefereeCareerTarget : TargetRules
{
	public RefereeCareerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("RefereeCareer");
	}
}
