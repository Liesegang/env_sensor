"""Thin PETG FDM front/rear case with local connector and cable relief.

Coordinates match the populated reference stack. The central body is 7.65mm;
only the side-entry battery connector needs a raised 9.75mm local cover.
"""
from pathlib import Path
import math
import FreeCAD as App
import Part
import MeshPart
from build_stack_reference import hex_prism, wire_path, RETENTION_POINTS

HERE=Path(__file__).resolve().parent
OUT=HERE/'generated'
SCREW_CENTERS=((-38.,22.),(-38.,-22.),(28.,-7.))
# side, hook start Y, tongue start/end Y, free end
SNAPS=((-1,-22.,-27.,-15.6,'high'),(-1,17.,15.6,27.,'low'),
       (1,-15.,-15.,-3.5,'low'),(1,17.,16.,27.,'low'))


def box(x0,x1,y0,y1,z0,z1):
    return Part.makeBox(x1-x0,y1-y0,z1-z0,App.Vector(x0,y0,z0))


def rounded_prism(hx,hy,r,z0,z1):
    s=box(-hx,hx,-hy,hy,z0,z1)
    vertical=[e for e in s.Edges if e.BoundBox.ZLength>z1-z0-1e-5]
    return s.makeFillet(r,vertical)


def translated_rounded(x0,x1,y0,y1,r,z0,z1):
    s=rounded_prism((x1-x0)/2,(y1-y0)/2,r,z0,z1)
    s.translate(App.Vector((x0+x1)/2,(y0+y1)/2,0))
    return s


def hook_length(sign,y):
    # Shortened right lower hook clears the antenna's1.5mm resin keepout.
    return 4.8 if sign>0 and y<-14. else 5.0


def hook(sign,y):
    # 0.5mm radial extension, 0.3mm overlap into the 0.9mm front tongue;
    # 0.9mm insertion ramp followed by a 0.4mm retaining shoulder.
    x0,x1=sign*44.4,sign*44.9
    pts=[App.Vector(x0,y,-.4),App.Vector(x1,y,.5),App.Vector(x1,y,.9),App.Vector(x0,y,.9),App.Vector(x0,y,-.4)]
    return Part.Face(Part.makePolygon(pts)).extrude(App.Vector(0,hook_length(sign,y),0))


def access_cuts(shape):
    # Both overlapping front/rear walls need the same outside connector path.
    # Actual SW1 body and lever S1 stick beyond the +X board edge.
    shape=shape.cut(box(43.1,46.5,-25.85,-16.15,-.2,5.2))
    shape=shape.cut(box(43.1,46.5,-2.6,14.1,-.2,3.1))
    shape=shape.cut(box(30.75,40.50,27.9,30.4,-.2,3.65))
    # Socket mouth is recessed1.175mm from the outside edge. Allow the
    # assumed11.5x5.7mm cable overmold into only the outer1.3mm of wall.
    shape=shape.cut(box(29.875,41.375,28.9,30.4,-1.27,4.43))
    # microSD insertion/ejection uses +Y. The card path crosses the shell.
    shape=shape.cut(box(-7.4,4.3,27.0,30.4,0.,2.2))
    # ES4L15BA1 resin clearance per datasheet:1.5mm around the module.
    shape=shape.cut(box(32.975,44.725,-10.132,-3.682,-1.5,2.6))
    return shape


def create_front():
    # Two 0.2mm layers at the viewing lip. 0.05mm PET adhesive locates the glass
    # face at -3.40; four rear silicone pads hold its non-display borders.
    base=rounded_prism(45.5,30.2,3.2,-3.85,-2.35)
    base=base.cut(rounded_prism(43.6,28.3,.2,-3.45,-2.34))
    base=base.cut(box(-35.8,41.,-25.7,25.7,-3.86,-2.34))
    base=base.cut(box(-44.35,-43.15,-15.3,15.3,-2.70,-2.30))
    sleeve=rounded_prism(45.5,30.2,3.2,-2.35,1.4)
    sleeve=sleeve.cut(rounded_prism(44.6,29.3,2.5,-2.36,1.41))
    front=base.fuse(sleeve)
    for sign,y,lo,hi,free in SNAPS:
        x0,x1=(44.5,45.6) if sign>0 else (-45.6,-44.5)
        # Full-depth slots make a horizontal cantilever 11..11.5mm long. The slot
        # is 0.6mm wide, printable with a 0.4mm nozzle, and stops above base.
        front=front.cut(box(x0,x1,lo,hi,-1.0,-.4))
        e0,e1=(hi,hi+.6) if free=='high' else (lo-.6,lo)
        front=front.cut(box(x0,x1,e0,e1,-1.0,1.41))
        # Closing shoulder has0.05mm axial play and a0.45mm upper band
        # (at least two0.2mm layers).
        front=front.cut(box(x0,x1,y-.1,y+hook_length(sign,y)+.1,-.05,.95))
        # A single-layer breakaway membrane supports the free end while
        # printing the first horizontal tongue layer across the11mm gap.
        # Cut all four0.2mm membranes after printing to free the tongues.
        b0,b1=(44.6,45.5) if sign>0 else (-45.5,-44.6)
        front=front.fuse(box(b0,b1,e0-.01,e1+.01,-.4,-.2))
    return access_cuts(front).removeSplitter()


def screw_boss(rear,x,y):
    # Outside-accessible M3 nut well:1.0mm PETG seat between nut andPCB.
    # M3x5 crosses PCB1.6 +case1.0 +nut2.4mm; nut ends0.4 below the back.
    # BossR4.1 leaves0.809mm at hex corners for two0.4mm perimeters.
    rear=rear.fuse(Part.makeCylinder(4.1,3.8,App.Vector(x,y,0)))
    rear=rear.cut(hex_prism(5.7,1.0,3.82,x,y))
    rear=rear.cut(Part.makeCylinder(1.7,3.84,App.Vector(x,y,-.02)))
    return rear


def create_rear():
    # Lower sleeve fits inside the front. Upper shoulder lands on its Z1.4
    # top edge to define the closed position and prevent over-compression.
    lower=rounded_prism(44.4,29.1,2.5,-2.35,1.4)
    upper=rounded_prism(45.5,30.2,3.2,1.4,3.8)
    rear=lower.fuse(upper)
    rear=rear.cut(rounded_prism(43.5,28.2,.2,-2.36,3.0))
    for x,y in SCREW_CENTERS: rear=screw_boss(rear,x,y)
    for x,y in RETENTION_POINTS:
        sign=1 if y>0 else -1
        a,b=(26.2,28.2) if sign>0 else (-28.2,-26.2)
        c,d=(28.1,29.1) if sign>0 else (-29.1,-28.1)
        seat=box(x-2.3,x+2.3,a,b,-2.40,-2.00)
        bridge=box(x-2.3,x+2.3,c,d,-2.35,-2.00)
        rear=rear.fuse(seat.fuse(bridge))
    # Local roof only above JST PH housing and mated plug, with0.2mm clearance.
    hump=translated_rounded(8.18,16.28,8.94,21.04,.8,3.,5.9)
    rear=rear.fuse(hump)
    rear=rear.cut(translated_rounded(8.98,15.48,9.74,20.24,.2,2.99,5.1))
    # Wire bundle: diameter1.2 plus0.1 radial allowance, exactly along lead.
    rear=rear.cut(wire_path(.7))
    for x in (15.5,18.,20.5,23.):
        rear=rear.cut(box(x-.55,x+.55,-12.,8.,2.95,3.85))
    # Sensor inlet/outlet path enters along -Y and leaves the top slits.
    rear=rear.cut(box(15.,24.,-30.4,-27.9,.25,2.4))
    rear=rear.cut(Part.makeCylinder(1.2,1.,App.Vector(21.,-25.,2.95)))
    # Side sound port is along -X after the actual buzzer's R180 placement.
    for dx,dy in ((0,0),(-2.5,0),(2.5,0),(0,-2.5),(0,2.5)):
        rear=rear.cut(Part.makeCylinder(.65,1.,App.Vector(-28.8+dx,21+dy,2.95)))
    # Locally remove only the lower -X wall surrounding the folded flex.
    rear=rear.cut(box(-44.5,-43.35,-15.3,15.3,-2.6,1.6))
    rear=rear.fuse(translated_rounded(30.05,41.20,20.60,29.0,.7,3.0,4.2))
    rear=rear.cut(box(30.75,40.50,21.30,28.1,2.99,3.60))
    rear=access_cuts(rear)
    rear=rear.cut(box(38.5,43.5,-25.85,-16.15,2.65,3.85))
    for sign,y,_,_,_ in SNAPS: rear=rear.fuse(hook(sign,y))
    return rear.removeSplitter()


def export_stl(shape,path,orientation):
    s=shape.copy()
    if orientation=='front':
        s.translate(App.Vector(0,0,3.85))
    else:
        # Back-side down: the CN1 dome rests on the bed. The main back
        # face is 2.1mm above it and needs slicer support underneath.
        s.rotate(App.Vector(),App.Vector(1,0,0),180)
        s.translate(App.Vector(0,0,-s.BoundBox.ZMin))
    mesh=MeshPart.meshFromShape(Shape=s,LinearDeflection=.06,AngularDeflection=.12,Relative=False)
    mesh.write(str(path))



def create_assembly(case_doc):
    stack=App.openDocument(str(OUT/'card_env_sensor_stack_reference.FCStd'))
    assembly=App.newDocument('CardEnvSensor_CaseAssembly')
    shells=assembly.addObject('App::DocumentObjectGroup','CaseParts')
    refs=assembly.addObject('App::DocumentObjectGroup','ReferenceAssembly')
    step_parts=[]
    for source in list(case_doc.Objects)+list(stack.Objects):
        if not hasattr(source,'Shape'): continue
        if hasattr(source,'FitRole') and source.FitRole=='Keepout': continue
        obj=assembly.addObject('PartDesign::Feature',source.Name)
        obj.Label=source.Label
        obj.Shape=source.Shape.copy()
        role=source.FitRole if hasattr(source,'FitRole') else 'Case'
        obj.addProperty('App::PropertyString','FitRole','Fit')
        obj.FitRole=role
        for key in ('MechanicalSource','PopulationAssumption','Source','Tolerance',
                    'CompressionRange','Assumption','PrintOrientation','SnapPreparation'):
            if key in source.PropertiesList:
                obj.addProperty('App::PropertyString',key,'Reference')
                setattr(obj,key,str(getattr(source,key)))
        obj.Visibility=True
        (shells if role=='Case' else refs).addObject(obj)
        if role!='Keepout': step_parts.append(obj)
        if obj.ViewObject is not None:
            if source.ViewObject is not None: obj.ViewObject.ShapeColor=source.ViewObject.ShapeColor
            if role=='Keepout': obj.ViewObject.Visibility=False
    assembly.recompute()
    assembly.saveAs(str(OUT/'card_env_sensor_case_assembly.FCStd'))
    Part.export(step_parts,str(OUT/'card_env_sensor_case_assembly.step'))
    print(f'Assembly exported: {len(step_parts)} shell / physical reference objects')
    App.closeDocument(assembly.Name)
    App.closeDocument(stack.Name)


def main():
    OUT.mkdir(exist_ok=True)
    front,rear=create_front(),create_rear()
    for name,s in (('front',front),('rear',rear)):
        if not s.isValid() or len(s.Solids)!=1: raise ValueError(f'{name}: not one valid solid ({len(s.Solids)})')
    doc=App.newDocument('CardEnvSensor_CaseDraft')
    for name,s,color in (('Front',front,(.14,.17,.20)),('Rear',rear,(.24,.27,.30))):
        obj=doc.addObject('PartDesign::Feature',name)
        obj.Label=name+' case: PETG FDM 0.4mm nozzle /0.2mm layers'
        obj.Shape=s
        obj.addProperty('App::PropertyString','PrintOrientation','Manufacture')
        if name=='Front':
            obj.addProperty('App::PropertyString','SnapPreparation','Manufacture')
            obj.SnapPreparation='Cut four0.2mm breakaway membranes across free-end0.6mm slots; check11mm bridge print quality'
        obj.PrintOrientation='Front lip on bed' if name=='Front' else 'Back side down; CN1 dome on bed; main back face 2.1mm high: supports required'
        if obj.ViewObject is not None: obj.ViewObject.ShapeColor=color
        Part.export([obj],str(OUT/f'card_env_sensor_case_{name.lower()}_draft.step'))
        export_stl(s,OUT/f'card_env_sensor_case_{name.lower()}_print.stl',name.lower())
    doc.recompute()
    doc.saveAs(str(OUT/'card_env_sensor_case_draft.FCStd'))
    create_assembly(doc)
    print(f'case XY: {front.BoundBox.XLength:.2f} x {front.BoundBox.YLength:.2f} mm; body7.65; CN1 local9.75')
    print(f'valid single solids: front{len(front.Solids)}, rear{len(rear.Solids)}; volume{front.Volume:.1f}/{rear.Volume:.1f} mm3')
    print('USB local roof: inner3.6 / outer4.2 / thickness0.6mm')
    print('Four11..11.5mm front flex tongues; snap overlap0.3; axialplay0.05; rear seated atZ1.4')
    print('M3x5 with outside M3 nuts atZ1.0..3.4; AF5.7 through wells, seat1.0, bossR4.1, recess0.4')

if __name__=='__main__': main()
