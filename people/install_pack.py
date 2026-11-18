import bpy, os, sys
from bl_ext.user_default.mpfb.services.assetservice import AssetService
from bl_ext.user_default.mpfb.services.locationservice import LocationService

z = sys.argv[sys.argv.index("--") + 1]
target = LocationService.get_user_data()
print("TARGET", target)
print("CHECK", AssetService.check_asset_pack_zip(z))
print("RESULT", AssetService.fix_and_extract_asset_pack_zip(z, target))
AssetService.update_all_asset_lists()
for sub in ["proxymeshes", "skins", "clothes", "hair", "eyebrows", "eyes"]:
    roots = AssetService.get_asset_roots(sub)
    names = []
    for r in roots:
        if os.path.isdir(r):
            names += sorted(os.listdir(r))
    print("LIST", sub, names)
