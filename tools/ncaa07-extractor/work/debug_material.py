import sys
import bpy
args = sys.argv[sys.argv.index('--') + 1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=args[0])
for mat in bpy.data.materials:
    if mat.name.startswith(args[1]):
        print('MAT', mat.name, getattr(mat, 'surface_render_method', None), getattr(mat, 'blend_method', None))
        for n in mat.node_tree.nodes:
            extra = ''
            if n.type == 'TEX_IMAGE':
                extra = (n.image.name, n.image.alpha_mode, n.image.colorspace_settings.name, n.image.channels, n.image.depth)
            print('  NODE', n.type, n.name, extra)
        for l in mat.node_tree.links:
            print('  LINK', l.from_node.name, l.from_socket.name, '->', l.to_node.name, l.to_socket.name)
