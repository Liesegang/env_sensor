"""Populated v2 board, real footprint STEP models and assembly keepouts."""
from pathlib import Path
import json
import math
import FreeCAD as App
import Part
from build_pcb_model import board_xml

HERE = Path(__file__).resolve().parent
OUT = HERE / 'generated'
LIBRARY = HERE / 'fusion_libraries'
SCREW_CENTERS = ((-38.,22.),(-38.,-22.),(28.,-7.))
RETENTION_POINTS = ((-30.,-26.75),(-30.,26.75),(30.,-26.75),(26.,26.75))


def box(b):
    return Part.makeBox(b[1]-b[0], b[3]-b[2], b[5]-b[4], App.Vector(b[0],b[2],b[4]))


def feature(doc, name, label, shape, color=(.65,.65,.68), role='Physical'):
    obj = doc.addObject('PartDesign::Feature', name)
    obj.Label, obj.Shape = label, shape
    obj.addProperty('App::PropertyString','FitRole','Fit')
    obj.FitRole = role
    obj.Visibility = role != 'Keepout'
    if obj.ViewObject is not None:
        obj.ViewObject.ShapeColor = color
        if role == 'Keepout':
            obj.ViewObject.Transparency = 80
    return obj


def place(shape, x, y, rotation):
    if 'M' in rotation:
        raise ValueError('Mirrored component needs explicit transform: '+rotation)
    angle = float(rotation.replace('S','').replace('R',''))
    s = shape.copy()
    s.Placement = App.Placement(App.Vector(x,y,0), App.Rotation(App.Vector(0,0,1),angle)).multiply(s.Placement)
    return s


def envelope(c, key='bbox'):
    b,p = c[key], c['placement']
    return place(box([b[k] for k in ('x0','x1','y0','y1','z0','z1')]), p['x'],p['y'],p['rotation'])


def wire_path(radius):
    pts = [App.Vector(10.4,8.85,2.55),App.Vector(12.23,10.3,2.55),App.Vector(12.23,12.1,2.55)]
    solids = [Part.makeSphere(radius,p) for p in pts]
    for a,b in zip(pts,pts[1:]):
        d=b-a
        solids.append(Part.makeCylinder(radius,d.Length,a,d))
    s=solids[0]
    for q in solids[1:]: s=s.fuse(q)
    return s.removeSplitter()


def fpc_ribbon(thickness, width):
    y=1.31
    def v(x,z): return App.Vector(x,y,z)
    a,b,c,d,e,f=(v(-43.3,-2.53),v(-43.4,-2.53),v(-44.,-1.93),v(-44.,.1),v(-43.4,.7),v(-33.65,.7))
    k=.6/math.sqrt(2)
    path=Part.Wire([Part.makeLine(a,b),Part.Arc(b,v(-43.4-k,-1.93-k),c).toShape(),Part.makeLine(c,d),Part.Arc(d,v(-43.4-k,.1+k),e).toShape(),Part.makeLine(e,f)])
    p=Part.makePolygon([App.Vector(a.x,y-width/2,a.z-thickness/2),App.Vector(a.x,y+width/2,a.z-thickness/2),App.Vector(a.x,y+width/2,a.z+thickness/2),App.Vector(a.x,y-width/2,a.z+thickness/2),App.Vector(a.x,y-width/2,a.z-thickness/2)])
    return path.makePipeShell([Part.Wire(p.Edges)],True,False)


def hex_prism(af,z0,z1,x=0,y=0):
    r=af/math.sqrt(3)
    pts=[App.Vector(x+r*math.cos(i*math.pi/3),y+r*math.sin(i*math.pi/3),z0) for i in range(7)]
    return Part.Face(Part.makePolygon(pts)).extrude(App.Vector(0,0,z1-z0))


def main():
    OUT.mkdir(exist_ok=True)
    pdoc=App.openDocument(str(OUT/'card_env_sensor_v2_pcb.FCStd'))
    pcb=pdoc.getObject('PCB').Shape.copy()
    App.closeDocument(pdoc.Name)
    doc=App.newDocument('CardEnvSensor_StackReference')
    feature(doc,'PCB','PCB v2: 1.6 mm, source outline and drilled holes',pcb,(.04,.29,.25))
    panel=feature(doc,'EPD','Bare EPD: 86.6 x 56 x 0.78 mm',box((-43.3,43.3,-28,28,-3.40,-2.62)),(.83,.82,.75))
    panel.addProperty('App::PropertyString','Tolerance','Dimensions')
    panel.Tolerance='Thickness 0.78 +/-0.07 mm; front -3.40, back -2.69..-2.55'
    feature(doc,'EPDMaxThickness','EPD maximum thickness: rear face -2.55',box((-43.3,43.3,-28,28,-3.40,-2.55)),(.8,.6,.2),'Keepout')
    battery=feature(doc,'Battery','DTP502535(PHR): nominal 25 x 36 x 5 mm',box((-14,11,-27.75,8.25,-2.3,2.7)),(.45,.49,.57))
    battery.addProperty('App::PropertyString','Source','Dimensions')
    battery.Source='https://www.sengoku.co.jp/mod/sgk_cart/detail.php?code=EEHD-4YZL'
    feature(doc,'BatteryFixingPad','Battery 15 x 20 x 0.3 mm adhesive pad',box((-9,6,-24,-4,2.7,3.0)),(.3,.5,.6),'Compliant')
    feature(doc,'BatteryWire','Battery lead bundle: diameter 1.2 mm to CN1',wire_path(.6),(.65,.15,.12))
    feature(doc,'BatteryWireKeepout','Battery wire: 0.1 mm radial relief',wire_path(.7),(.8,.6,.2),'Keepout')
    tape,pads=[],[]
    for x,y in RETENTION_POINTS:
        tape.append(box((x-2,x+2,y-.5,y+.5,-3.45,-3.40)))
        pads.append(box((x-2,x+2,y-.5,y+.5,-2.62,-2.40)))
    feature(doc,'PanelFrontTape','Four glass-border seats: 0.05 mm PET adhesive tape',Part.makeCompound(tape),(.3,.5,.6),'Compliant')
    o=feature(doc,'PanelRetentionPads','Four silicone pads: 0.30 mm free, 0.22 mm compressed',Part.makeCompound(pads),(.3,.5,.6),'Compliant')
    o.addProperty('App::PropertyString','CompressionRange','Dimensions')
    o.CompressionRange='Seat -2.40; compressed 0.15..0.29 mm across EPD thickness tolerance'
    for i,(x,y) in enumerate(SCREW_CENTERS,1):
        screw=Part.makeCylinder(3,.7,App.Vector(x,y,-2.3)).fuse(Part.makeCylinder(1.5,5,App.Vector(x,y,-1.6)))
        feature(doc,f'Screw{i}',f'FLA-0305E M3x5 screw {i}: D6 x H0.7',screw)
        feature(doc,f'ScrewHeadMax{i}',f'Screw {i}: maximum H0.85',Part.makeCylinder(3,.85,App.Vector(x,y,-2.45)),(.8,.6,.2),'Keepout')
        nut=hex_prism(5.5,1.0,3.4,x,y).cut(Part.makeCylinder(1.25,2.5,App.Vector(x,y,.95)))
        feature(doc,f'Nut{i}',f'M3 outside nut {i}: AF5.5 x2.4mm, recessed0.4mm',nut)
        feature(doc,f'NutOutsideAccess{i}',f'Outside insertion passage for nut {i}',hex_prism(5.55,1.0,6.0,x,y),(.8,.6,.2),'Keepout')
    model_map=json.loads((LIBRARY/'model_map.json').read_text())
    by_ref={ref:m for m in model_map['models'] for ref in m['refs']}
    count=0
    board=board_xml()
    for el in board.find('elements'):
        ref=el.get('name'); x,y=float(el.get('x')),float(el.get('y'))
        if not (-43.3<=x<=43.3 and -28<=y<=28): continue
        m=by_ref[ref]
        if m['status']=='excluded': continue
        if m['status']!='ready': raise ValueError(f'Missing model for {ref}')
        if m.get('population_pending'):
            shape=Part.makeCompound([Part.makeCylinder(.35,.035,App.Vector(px,0,0)) for px in (-3.81,-2.54,-1.27,0,1.27,2.54)])
        else: shape=Part.read(str(LIBRARY/m['filename']))
        obj=feature(doc,ref,f"{ref}: {el.get('value')} [{m['model_kind']}]",place(shape,x,y,el.get('rot','R0')))
        obj.addProperty('App::PropertyString','MechanicalSource','Dimensions')
        obj.MechanicalSource=m['filename']+' | '+m['notes']
        if m.get('population_pending'):
            obj.addProperty('App::PropertyString','PopulationAssumption','Dimensions')
            obj.PopulationAssumption='Debug header unconfirmed: exposed pads; fitted pins require clearance revision'
        count+=1
    # All fitted through-hole pad locations receive the user's0.5mm cut-leg
    # allowance, including solder at the underside, plus0.1mm radial margin.
    # Board holes and vias do not imply leads and are deliberately excluded.
    packages={(l.get('name'),l.get('urn'),p.get('name')):p for l in board.find('libraries') for p in l.findall('./packages/package')}
    legs=[];leg_refs=[]
    for el in board.find('elements'):
        ref=el.get('name');m=by_ref[ref]
        x,y=float(el.get('x')),float(el.get('y'))
        if not (-43.3<=x<=43.3 and -28<=y<=28): continue
        if m['status']!='ready' or m.get('population_pending'): continue
        package=packages[(el.get('library'),el.get('library_urn'),el.get('package'))]
        for pad in package.findall('pad'):
            r=float(pad.get('drill'))/2+.1
            leg=Part.makeCylinder(r,.5,App.Vector(float(pad.get('x')),float(pad.get('y')),-2.1))
            legs.append(place(leg,x,y,el.get('rot','R0')))
            leg_refs.append(ref+'/'+pad.get('name'))
    legs_obj=feature(doc,'SolderLegKeepout','Fitted through-hole pad solder / cut-leg allowance:0.5mm +0.1mm radius',Part.makeCompound(legs),(.8,.6,.2),'Keepout')
    legs_obj.addProperty('App::PropertyInteger','PadCount','Dimensions')
    legs_obj.PadCount=len(legs)
    legs_obj.addProperty('App::PropertyString','PadReferences','Dimensions')
    legs_obj.PadReferences=', '.join(leg_refs)
    for c in json.loads((HERE/'component_envelopes.json').read_text())['components']:
        feature(doc,'Envelope_'+c['ref'],c['ref']+' tolerance body envelope',envelope(c),(.8,.6,.2),'Keepout')
        if 'mated_bbox' in c:
            feature(doc,'Mated_'+c['ref'],c['ref']+' mated plug envelope',envelope(c,'mated_bbox'),(.8,.6,.2),'Keepout')
        if c['ref']=='SD1':
            b=dict(c['card_locked_bbox']);b['x1']+=c['card_ejected_projection']-c['card_locked_projection']
            card=dict(c,bbox=b)
            feature(doc,'SDCardEjected','microSD ejected envelope: +Y access',envelope(card),(.8,.6,.2),'Keepout')
    s1_motion=Part.makeCylinder(8.25,2.9,App.Vector(38.1,5.75,0)).fuse(Part.makeCylinder(8.25,2.9,App.Vector(37.2,5.75,0)))
    feature(doc,'S1Operation','TMHU27 R7.85 +0.2 tolerance +0.2 clearance; +/-25 degrees and 0.9mm press',s1_motion.removeSplitter(),(.8,.6,.2),'Keepout')
    rf=feature(doc,'IC5ResinKeepout','ES4L15BA1 resin clearance:1.5mm per manufacturer',box((32.975,44.725,-10.132,-3.682,-1.5,2.6)),(.8,.6,.2),'Keepout')
    rf.addProperty('App::PropertyString','Source','Dimensions')
    rf.Source='docs/datasheets/ES4L15BA1_DataSheet_V1_0_20260624E.pdf p.33: g>=1.5mm, g>=resin thickness, thickness<5mm'
    usb_access=feature(doc,'USBPlugAccess','USB cable overmold access: assumed11.5 x5.7mm, outer wall recess1.3mm',box((29.875,41.375,28.9,30.4,-1.27,4.43)),(.8,.6,.2),'Keepout')
    usb_access.addProperty('App::PropertyString','Assumption','Dimensions')
    usb_access.Assumption='Representative cable plastic overmold11.5x5.7mm; not a universal cable limit; mouth beginsY28.9'
    feature(doc,'SW1Operation','Slide actuator movement and finger access',box((43.25,46.15,-23.75,-18.25,2.65,5)),(.8,.6,.2),'Keepout')
    feature(doc,'FPCBondKeepout','EPD FPC base UV glue: module maximum thickness1.20 mm',box((-43.3,-40.,-11.5,14.1,-2.62,-2.20)),(.8,.6,.2),'Keepout')
    feature(doc,'FPC','EPD flex: 25.5 mm wide, two R0.6 folds to J1',fpc_ribbon(.18,25.5),(.75,.4,.1))
    o=feature(doc,'FPCKeepout','FPC 0.10 mm thickness and edge allowance',fpc_ribbon(.38,25.7),(.8,.6,.2),'Keepout')
    o.addProperty('App::PropertyString','Assumption','Dimensions')
    o.Assumption='Provisional flex thickness 0.18; fold radius 0.6; landscape centre Y=1.31 mm'
    doc.recompute()
    doc.saveAs(str(OUT/'card_env_sensor_stack_reference.FCStd'))
    physical=[o for o in doc.Objects if hasattr(o,'Shape') and o.FitRole in ('Physical','Compliant')]
    Part.export(physical,str(OUT/'card_env_sensor_stack_reference.step'))
    print(f'Populated references: {count}; SFA40 excluded; CON3 unconfirmed pads')
    print('EPD maximum back -2.55 / screw max front -2.45: worst-case gap 0.10 mm')
    print('Battery -2.30..2.70 with 0.30 mm fixing pad; EPD compressed retention pads 0.15..0.29 mm')

if __name__=='__main__': main()
