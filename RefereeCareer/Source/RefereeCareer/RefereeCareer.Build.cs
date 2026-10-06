using UnrealBuildTool;

public class RefereeCareer : ModuleRules
{
	public RefereeCareer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"DeveloperSettings", "Slate", "SlateCore", "AIModule", "UMG"
		});

		// Public/RefCore and Private/RefCore are plain C++ (standard library only) shared with Tests/Core,
		// where they are unit-tested and balance-simulated without the engine.
	}
}
