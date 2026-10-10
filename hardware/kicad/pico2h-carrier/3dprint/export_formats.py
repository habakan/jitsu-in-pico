import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile

import numpy as np
import trimesh


def export_formats(parts, out, origin):
    plates = [
        ("01_base", ["base"]),
        ("02_front", ["lid"]),
        ("03_small_parts", ["camera_retainer", "button_1", "button_2"]),
    ]
    for plate, names in plates:
        model = ET.Element(
            "model",
            unit="millimeter",
            xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02",
        )
        resources = ET.SubElement(model, "resources")
        build = ET.SubElement(model, "build")
        x = 10.0
        for i, name in enumerate(names, 1):
            mesh = next(p["print_mesh"] for p in parts if p["name"] == name).copy()
            mesh.apply_translation([x, 10, 0])
            x += mesh.extents[0] + 10
            obj = ET.SubElement(resources, "object", id=str(i), type="model", name=name)
            surface = ET.SubElement(obj, "mesh")
            vertices = ET.SubElement(surface, "vertices")
            triangles = ET.SubElement(surface, "triangles")
            for v in mesh.vertices:
                ET.SubElement(
                    vertices,
                    "vertex",
                    **{axis: f"{value:.6f}" for axis, value in zip("xyz", v)},
                )
            for face in mesh.faces:
                ET.SubElement(
                    triangles,
                    "triangle",
                    **{f"v{j + 1}": str(v) for j, v in enumerate(face)},
                )
            ET.SubElement(build, "item", objectid=str(i))
        path = out / f"bambu_plate_{plate}.3mf"
        with ZipFile(path, "w", ZIP_DEFLATED) as archive:
            archive.writestr(
                "[Content_Types].xml",
                '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>',
            )
            archive.writestr(
                "_rels/.rels",
                '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>',
            )
            archive.writestr(
                "3D/3dmodel.model",
                ET.tostring(model, encoding="utf-8", xml_declaration=True),
            )
        with ZipFile(path) as archive:
            loaded = ET.fromstring(archive.read("3D/3dmodel.model"))
        objects = loaded.findall(".//{*}object")
        if loaded.attrib["unit"] != "millimeter" or len(objects) != len(names):
            raise ValueError(f"3MF object count: {plate}")
        for name, obj in zip(names, objects):
            vertices = [
                [float(v.attrib[a]) for a in "xyz"] for v in obj.findall(".//{*}vertex")
            ]
            faces = [
                [int(f.attrib[a]) for a in ("v1", "v2", "v3")]
                for f in obj.findall(".//{*}triangle")
            ]
            mesh = trimesh.Trimesh(vertices=vertices, faces=faces)
            expected = next(p["print_mesh"] for p in parts if p["name"] == name)
            if not mesh.is_watertight or not np.allclose(
                mesh.extents, expected.extents, atol=1e-5
            ):
                raise ValueError(f"3MF roundtrip: {name}")

    for category in ("case", "components"):
        lines = ["#VRML V2.0 utf8"]
        for part in parts:
            if part["name"] == "carrier":
                continue
            is_case = part["printable"] or part["name"] == "case_screw_heads"
            if is_case != (category == "case"):
                continue
            vertices = part["mesh"].vertices.copy() - origin
            vertices[:, 1] *= -1
            vertices /= 2.54
            color = " ".join(
                f"{int(part['color'][i : i + 2], 16) / 255:.5f}" for i in (1, 3, 5)
            )
            points = ",\n".join(
                " ".join(f"{v:.6f}" for v in vertex) for vertex in vertices
            )
            faces = ",\n".join(
                " ".join(map(str, [*face[::-1], -1])) for face in part["mesh"].faces
            )
            lines.append(
                f"Shape {{ appearance Appearance {{ material Material {{ diffuseColor {color} }} }} geometry IndexedFaceSet {{ solid TRUE coord Coordinate {{ point [\n{points}\n] }} coordIndex [\n{faces}\n] }} }}"
            )
        (out / f"{category}.wrl").write_text("\n".join(lines) + "\n")
