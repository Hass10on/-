"""Builds every Blender asset for Referee Career and (optionally) renders the preview images.

  With Blender:          blender --background --python Tools/Blender/build_all.py -- [--render] [--fast]
  With the bpy module:   python Tools/Blender/build_all.py [--render] [--fast]

Outputs: RefereeCareer/SourceArt/Meshes/*.fbx, RefereeCareer/Content/Data/venue_*.json, docs/images/*.png
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import build_community  # noqa: E402
import build_props  # noqa: E402
import build_stadium  # noqa: E402

if __name__ == "__main__":
    args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    build_stadium.main()
    build_community.main()
    build_props.main()
    if "--render" in args:
        # Rendering resets the scene several times; run it as a separate process so this one stays clean.
        cmd = [sys.executable, os.path.join(HERE, "render_previews.py")] + [a for a in args if a == "--fast"]
        if os.path.basename(sys.executable).lower().startswith("blender"):
            cmd = [sys.executable, "--background", "--python", os.path.join(HERE, "render_previews.py"), "--"] + [a for a in args if a == "--fast"]
        subprocess.run(cmd, check=True)
