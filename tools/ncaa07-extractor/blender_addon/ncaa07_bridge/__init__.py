"""NCAA 07 Bridge: the extractor sends models into this Blender (no new windows).

Install once (the extractor's "Install Blender add-on" does it), or run Blender
with  --python <this file>  for one session. While Blender is open the add-on
polls an inbox folder; the extractor drops a request there and the model is
imported into the current scene, organized:

  Players  armature + configured parts in collections Body / Head / Helmet /
           Hair / Decals; every alternative helmet, facemask, hair style, hand
           pose, shadow mesh... in a hidden "<player> Variants" collection.
           View3D sidebar > NCAA 07 swaps options per group.
  Stadiums imported as one collection, framed.
  Fields   turf, lines, numbers, logo, end zones and goal posts assembled.
"""
bl_info = {
    "name": "NCAA 07 Bridge",
    "author": "NFL2K5-PC",
    "version": (1, 0, 0),
    "blender": (4, 0, 0),
    "location": "View3D > Sidebar > NCAA 07",
    "description": "Receive NCAA 07 extractor models in this Blender; player variant switcher",
    "category": "Import-Export",
}

import json
import math
import os
import time
import traceback
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

INBOX = Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'NCAA07Bridge'
CATEGORIES = ('Body', 'Head', 'Helmet', 'Hair', 'Decal')


# ---------------------------------------------------------------- helpers

def _collection(name, parent=None):
    col = bpy.data.collections.new(name)
    (parent or bpy.context.scene.collection).children.link(col)
    return col


def _layer_collection(col, layer=None):
    layer = layer or bpy.context.view_layer.layer_collection
    if layer.collection == col:
        return layer
    for child in layer.children:
        found = _layer_collection(col, child)
        if found:
            return found
    return None


def _move(ob, col):
    for c in list(ob.users_collection):
        c.objects.unlink(ob)
    col.objects.link(ob)


def import_glb(path, into):
    """Import a GLB into collection `into`; returns the new objects."""
    before = set(bpy.data.objects)
    layer = _layer_collection(into)
    if layer:
        bpy.context.view_layer.active_layer_collection = layer
    bpy.ops.import_scene.gltf(filepath=str(Path(path).resolve()))
    new = [o for o in bpy.data.objects if o not in before]
    helpers = bpy.data.collections.get('glTF_not_exported')     # bone display shapes
    keep = []
    for ob in new:
        if helpers and ob.name in helpers.objects:
            bpy.data.objects.remove(ob, do_unlink=True)
            continue
        if into not in ob.users_collection:
            _move(ob, into)
        keep.append(ob)
    if helpers and not helpers.objects:
        bpy.data.collections.remove(helpers)
    _fix_cutout_shadows(keep)
    return keep


def _fix_cutout_shadows(objects):
    """Cut-out materials (numbers, decals, facemask holes) cast their full
    rectangle as a shadow unless transparent shadows are on: a black box
    behind every jersey number."""
    for ob in objects:
        if ob.type != 'MESH':
            continue
        for mat in ob.data.materials:
            if not mat or not mat.use_nodes:
                continue
            bsdf = next((n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
            if bsdf and bsdf.inputs['Alpha'].links:
                if hasattr(mat, 'use_transparent_shadow'):
                    mat.use_transparent_shadow = True
                if hasattr(mat, 'shadow_method'):
                    mat.shadow_method = 'HASHED'


def frame(objects):
    pts = [o.matrix_world @ Vector(c) for o in objects if o.type == 'MESH' for c in o.bound_box]
    if not pts:
        return
    lo = Vector([min(p[i] for p in pts) for i in range(3)])
    hi = Vector([max(p[i] for p in pts) for i in range(3)])
    size = max((hi - lo).length, 1.0)
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'VIEW_3D':
                space = area.spaces.active
                space.region_3d.view_location = (lo + hi) * .5
                space.region_3d.view_distance = size * 1.2
                space.clip_end = max(space.clip_end, size * 20)
                if space.shading.type == 'SOLID':
                    space.shading.type = 'MATERIAL'


# ---------------------------------------------------------------- players

def _remove_gltf_helpers(objects):
    """The glTF importer's bone display shapes (an icosphere in a
    'glTF_not_exported' collection) and childless empties."""
    helpers = bpy.data.collections.get('glTF_not_exported')
    for ob in list(objects):
        if ob.name not in bpy.data.objects:
            continue
        if (helpers and ob.name in helpers.objects) or (ob.type == 'EMPTY' and not ob.children):
            bpy.data.objects.remove(ob, do_unlink=True)
    if helpers and not helpers.objects:
        bpy.data.collections.remove(helpers)


def _members(ob):
    try:
        return json.loads(ob.get('ncaa_options', '{}'))
    except ValueError:
        return {}


def _target(rig, ob, visible):
    name = rig['ncaa_player']
    root = bpy.data.collections[rig['ncaa_root_collection']]
    if visible:
        cat = ob.get('ncaa_category', 'Body')
        return bpy.data.collections.get(f'{name} {cat}') or _collection(f'{name} {cat}', root)
    variants = bpy.data.collections.get(f'{name} Variants')
    if variants is None:
        variants = _collection(f'{name} Variants', root)
        layer = _layer_collection(variants)
        if layer:
            layer.hide_viewport = True               # eye icon: hidden, one click away
    group = ob.get('ncaa_group', 'Other')
    label = f'{name} {group} options'
    return bpy.data.collections.get(label) or _collection(label, variants)


def organize_player(objects, name):
    """Configured parts by category; alternatives hidden under Variants."""
    rig = next((o for o in objects if o.type == 'ARMATURE'), None)
    root = objects[0].users_collection[0]
    choices = {}
    for ob in objects:
        if 'ncaa_choices' in ob:
            try:
                choices = json.loads(ob['ncaa_choices'])
            except ValueError:
                pass
    _remove_gltf_helpers(objects)
    objects = [o for o in objects if o.name in bpy.data.objects]
    if rig:
        rig.name = f'{name}_Rig'
        rig.show_in_front = True
        rig.data.display_type = 'STICK'
        for pb in rig.pose.bones:
            pb.custom_shape = None
        rig['ncaa_player'] = name
        rig['ncaa_root_collection'] = root.name
        rig['ncaa_choices'] = json.dumps(choices)
        # every decoded clip is kept with the file (unassigned actions are
        # otherwise dropped when the .blend is saved)
        new_actions = [a for a in bpy.data.actions if not a.get('ncaa_owner')]
        for act in new_actions:
            act.use_fake_user = True
            act['ncaa_owner'] = name
        if new_actions:
            rig['ncaa_actions'] = sorted(a.name for a in new_actions)
    for ob in objects:
        if ob.type != 'MESH':
            continue
        visible = bool(ob.get('ncaa_selected', 1)) and ob.get('ncaa_category') not in ('Shadow', 'Effect')
        if rig:
            _move(ob, _target(rig, ob, visible))
        ob.hide_render = not visible
        if ob.data and ob.data.polygons:
            ob.data.polygons.foreach_set('use_smooth', [True] * len(ob.data.polygons))
    return rig


def player_parts(rig):
    root = bpy.data.collections.get(rig.get('ncaa_root_collection', ''))
    if not root:
        return []
    return [o for o in root.all_objects if o.type == 'MESH' and 'ncaa_group' in o]


FOLLOW_HELMET = ('HelmGroove', 'EnvMap', 'HelmetPadPie')
HELMET_GROUPS = ('FaceMask', 'FaceMaskLow', 'FaceMaskTabs', 'ChinStrap', 'HelmDecal', 'HelmSideNum',
                 'HelmBackNumSing', 'HelmBackNumDoub', 'HelmetLogo', 'Visor', 'PrideStickers')
HELMET_INNER = {'HelmetStd': 'HelmetInnerStd', 'HelmetAdams': 'HelmetInnerAdam',
                'HelmetSchutt': 'HelmetInnerSchu', 'HelmetRevo': 'HelmetInnerRevo'}


def choices_of(rig):
    try:
        return json.loads(rig.get('ncaa_choices', '{}'))
    except ValueError:
        return {}


def set_option(rig, group, option):
    """Choose `option` for `group`; every part is shown when each group that
    lists it has one of its options chosen (as the game's Swap tables do)."""
    choices = choices_of(rig)
    choices[group] = option
    if group == 'Head' and option in HELMET_INNER:      # the helmet's companion groups
        for g in FOLLOW_HELMET:
            choices[g] = option
        choices['HelmetInner'] = HELMET_INNER[option]
        saved = choices.pop('_helmet_off', None)
        if saved:
            choices.update(saved)
    elif group == 'Head':                              # a hair style: no helmet parts
        off = {g: choices.get(g) for g in HELMET_GROUPS if choices.get(g)}
        if off:
            choices['_helmet_off'] = off
        for g in HELMET_GROUPS + FOLLOW_HELMET + ('HelmetInner',):
            choices[g] = None
    rig['ncaa_choices'] = json.dumps(choices)
    for ob in player_parts(rig):
        member = _members(ob)
        if not member or ob.get('ncaa_category') in ('Shadow', 'Effect'):
            continue
        visible = all(choices.get(g) in opts for g, opts in member.items())
        ob['ncaa_selected'] = int(visible)
        ob.hide_render = not visible
        _move(ob, _target(rig, ob, visible))


def import_player(path, name=None):
    name = name or Path(path).stem
    root = _collection(name)
    objects = import_glb(path, root)
    rig = organize_player(objects, name)
    if rig:
        for ob in bpy.context.selected_objects:
            ob.select_set(False)
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
    frame([o for o in objects if o.name in bpy.data.objects and o.type == 'MESH' and not o.hide_render])
    return rig


# ---------------------------------------------------------------- stadiums and fields

def import_stadium(path, name=None):
    name = name or Path(path).stem
    root = _collection(name)
    objects = import_glb(path, root)
    frame(objects)
    return objects


def build_field(job, name=None):
    """Turf, lines, numbers, logo, end zones (+ mirrored) and goal posts."""
    root = _collection(name or job.get('name', 'Stadium_With_Field'))
    objects = []
    if job.get('stadium'):
        objects += import_glb(job['stadium'], _collection('Stadium', root))
    field_col = _collection('Field', root)
    for layer in job.get('layers', []):
        obs = import_glb(layer['file'], field_col)
        roots = [o for o in obs if o.parent not in obs]
        for r in roots:
            r.location.z += layer['z']
        meshes = [o for o in obs if o.type == 'MESH']
        for ob in meshes:
            ob.name = 'Field_' + layer['role'] + '_' + ob.name
        objects += obs
        if layer['role'] == 'endzone':
            for r in roots:
                dup = r.copy()
                field_col.objects.link(dup)
                dup.matrix_world = Matrix.Rotation(math.pi, 4, 'Z') @ dup.matrix_world
                for ob in meshes:
                    child = ob.copy(); child.data = ob.data.copy()
                    field_col.objects.link(child); child.parent = dup
                    objects.append(child)
    if job.get('goalpost'):
        posts = _collection('Goalposts', root)
        for y, angle in ((-60, 0), (60, math.pi)):
            obs = import_glb(job['goalpost'], posts)
            for ob in [o for o in obs if o.parent not in obs]:
                ob.matrix_world = Matrix.Translation((0, y, 0)) @ Matrix.Rotation(angle, 4, 'Z') @ ob.matrix_world
            objects += obs
    frame(objects)
    return objects


# ---------------------------------------------------------------- bridge

def handle(request):
    action = request.get('action', 'import')
    if action == 'player':
        import_player(request['file'], request.get('name'))
    elif action == 'stadium':
        import_stadium(request['file'], request.get('name'))
    elif action == 'field':
        build_field(request['job'], request.get('name'))
    elif action == 'open_blend':
        # append everything from a .blend into a new collection (never replaces the open file)
        root = _collection(request.get('name') or Path(request['file']).stem)
        with bpy.data.libraries.load(request['file'], link=False) as (src, dst):
            dst.objects = list(src.objects)
        for ob in dst.objects:
            if ob is not None:
                root.objects.link(ob)
        frame([o for o in dst.objects if o is not None])
    elif action == 'import':
        import_stadium(request['file'], request.get('name'))
    elif action == 'ping':
        pass
    else:
        raise ValueError(f'unknown action {action}')


def _poll():
    try:
        INBOX.mkdir(parents=True, exist_ok=True)
        (INBOX / 'alive.json').write_text(json.dumps({'pid': os.getpid(), 'time': time.time(),
                                                      'blender': bpy.app.version_string}))
        for req in sorted((INBOX / 'inbox').glob('*.json')) if (INBOX / 'inbox').is_dir() else []:
            done = INBOX / 'done'
            done.mkdir(exist_ok=True)
            try:
                request = json.loads(req.read_text(encoding='utf-8'))
                handle(request)
                status = {'ok': True}
            except Exception as e:
                status = {'ok': False, 'error': str(e), 'trace': traceback.format_exc()}
                print('[NCAA07 Bridge]', traceback.format_exc())
            (done / req.name).write_text(json.dumps(status), encoding='utf-8')
            try:
                req.unlink()
            except OSError:
                pass
    except Exception:
        print('[NCAA07 Bridge] poll failed', traceback.format_exc())
    return 1.0


# ---------------------------------------------------------------- UI: variant switcher

def _rig(context):
    ob = context.active_object
    if ob and ob.type == 'MESH' and ob.find_armature():
        ob = ob.find_armature()
    return ob if ob and ob.type == 'ARMATURE' and 'ncaa_player' in ob else None


class NCAA07_OT_set_option(bpy.types.Operator):
    bl_idname = 'ncaa07.set_option'
    bl_label = 'Use option'
    bl_options = {'REGISTER', 'UNDO'}
    group: bpy.props.StringProperty()
    option: bpy.props.StringProperty()

    def execute(self, context):
        rig = _rig(context)
        if not rig:
            return {'CANCELLED'}
        set_option(rig, self.group, self.option)
        return {'FINISHED'}


class NCAA07_OT_play_clip(bpy.types.Operator):
    """Put this clip on the player and fit the timeline to it"""
    bl_idname = 'ncaa07.play_clip'
    bl_label = 'Play clip'
    bl_options = {'REGISTER', 'UNDO'}
    action: bpy.props.StringProperty()

    def execute(self, context):
        rig = _rig(context)
        act = bpy.data.actions.get(self.action)
        if not rig or not act:
            return {'CANCELLED'}
        ad = rig.animation_data or rig.animation_data_create()
        for track in list(ad.nla_tracks):            # leave "all clips" mode
            if track.name == 'NCAA07 all clips':
                ad.nla_tracks.remove(track)
        ad.action = act
        scene = context.scene
        scene.frame_start, scene.frame_end = int(act.frame_range[0]), int(act.frame_range[1])
        scene.frame_set(scene.frame_start)
        if context.screen and not context.screen.is_animation_playing:
            bpy.ops.screen.animation_play()
        return {'FINISHED'}


class NCAA07_OT_all_clips(bpy.types.Operator):
    """Line up every clip one after another (NLA), so Play runs them all"""
    bl_idname = 'ncaa07.all_clips'
    bl_label = 'Line up all clips'
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        rig = _rig(context)
        if not rig:
            return {'CANCELLED'}
        ad = rig.animation_data or rig.animation_data_create()
        for track in list(ad.nla_tracks):
            if track.name == 'NCAA07 all clips':
                ad.nla_tracks.remove(track)
        ad.action = None
        track = ad.nla_tracks.new()
        track.name = 'NCAA07 all clips'
        start = 1
        names = rig.get('ncaa_actions') or sorted(a.name for a in bpy.data.actions)
        for name in names:
            act = bpy.data.actions.get(name)
            if not act:
                continue
            strip = track.strips.new(name, start, act)
            strip.blend_type = 'REPLACE'
            start = int(strip.frame_end) + 1
        context.scene.frame_start, context.scene.frame_end = 1, max(1, start - 1)
        context.scene.frame_set(1)
        self.report({'INFO'}, f'{len(track.strips)} clips, {start - 1} frames; press Play')
        return {'FINISHED'}


class NCAA07_OT_join_visible(bpy.types.Operator):
    bl_idname = 'ncaa07.join_visible'
    bl_label = 'Join configured parts per category'
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        rig = _rig(context)
        if not rig:
            return {'CANCELLED'}
        name = rig['ncaa_player']
        for cat in CATEGORIES:
            col = bpy.data.collections.get(f'{name} {cat}')
            obs = [o for o in col.objects if o.type == 'MESH'] if col else []
            if len(obs) < 2:
                continue
            with context.temp_override(active_object=obs[0], selected_editable_objects=obs, object=obs[0]):
                bpy.ops.object.join()
            obs[0].name = f'{name}_{cat}'
        return {'FINISHED'}


class NCAA07_PT_panel(bpy.types.Panel):
    bl_label = 'NCAA 07'
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'NCAA 07'

    def draw(self, context):
        layout = self.layout
        alive = (INBOX / 'alive.json')
        layout.label(text='Bridge: listening' if bpy.app.timers.is_registered(_poll) else 'Bridge: off', icon='LINKED')
        rig = _rig(context)
        if not rig:
            layout.label(text='Select a player rig for its options')
            return
        layout.label(text=rig['ncaa_player'], icon='ARMATURE_DATA')
        names = rig.get('ncaa_actions')
        if names:
            box = layout.box()
            box.label(text=f'Animations ({len(names)} clips from the game)', icon='ACTION')
            box.operator('ncaa07.all_clips', icon='SEQ_STRIP_DUPLICATE')
            current = rig.animation_data.action.name if rig.animation_data and rig.animation_data.action else ''
            col = box.column(align=True)
            for name in names:
                op = col.operator('ncaa07.play_clip', text=name, depress=name == current, icon='PLAY')
                op.action = name
        choices = choices_of(rig)
        groups = {}
        for ob in player_parts(rig):
            for g, opts in _members(ob).items():
                if g in ('Shadow',) + FOLLOW_HELMET + ('HelmetInner',):
                    continue
                for o in opts:
                    groups.setdefault(g, set()).add(o)
        main = ('Head', 'FaceMask', 'LeftHand', 'RightHand', 'LeftElbow', 'RightElbow',
                'LeftWrist', 'RightWrist', 'LeftShoe', 'RightShoe', 'Visor', 'Numbers')
        for g in [g for g in main if g in groups] + sorted(g for g in groups if g not in main):
            opts = groups[g]
            if len(opts) < 2 and g != 'Visor':
                continue
            box = layout.box()
            box.label(text=g)
            flow = box.grid_flow(columns=2, even_columns=True)
            for o in sorted(opts):
                op = flow.operator('ncaa07.set_option', text=o, depress=choices.get(g) == o)
                op.group, op.option = g, o
        layout.operator('ncaa07.join_visible')


classes = (NCAA07_OT_set_option, NCAA07_OT_play_clip, NCAA07_OT_all_clips, NCAA07_OT_join_visible, NCAA07_PT_panel)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    if not bpy.app.timers.is_registered(_poll):
        bpy.app.timers.register(_poll, first_interval=1.0, persistent=True)


def unregister():
    if bpy.app.timers.is_registered(_poll):
        bpy.app.timers.unregister(_poll)
    for c in reversed(classes):
        bpy.utils.unregister_class(c)


if __name__ == '__main__':                 # blender --python __init__.py
    try:
        unregister()
    except Exception:
        pass
    register()
