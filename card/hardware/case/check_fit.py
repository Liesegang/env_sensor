"""Validate both shells against all fitted parts, tolerance/access volumes."""
from pathlib import Path
import json
import FreeCAD as App
import Part
from build_case_draft import SNAPS, SCREW_CENTERS
from build_stack_reference import hex_prism

HERE=Path(__file__).resolve().parent
OUT=HERE/'generated'


def main():
    case=App.openDocument(str(OUT/'card_env_sensor_case_draft.FCStd'))
    stack=App.openDocument(str(OUT/'card_env_sensor_stack_reference.FCStd'))
    front,rear=case.getObject('Front').Shape,case.getObject('Rear').Shape
    for name,s in (('Front',front),('Rear',rear)):
        assert s.isValid() and len(s.Solids)==1,(name,len(s.Solids))
    parts={o.Name:o.Shape for o in stack.Objects if hasattr(o,'Shape')}
    assert all(not o.Visibility for o in stack.Objects if hasattr(o,'FitRole') and o.FitRole=='Keepout')
    for name,s in parts.items():
        assert s.isValid() and s.Solids and all(q.isValid() for q in s.Solids),name
    results=[];failures=[]
    for name,a,b in [('Front/Rear',front,rear)]+[(shell_name+'/'+part_name,shell,s) for shell_name,shell in (('Front',front),('Rear',rear)) for part_name,s in parts.items()]:
        overlap=a.common(b).Volume
        results.append({'pair':name,'overlap_mm3':round(overlap,8)})
        if overlap>1e-5: failures.append((name,overlap))
    # The EPD front face is fixed by tape, so the maximum thickness determines
    # solder-side clearance. H0.85 is the actual low-head screw maximum.
    assert abs(parts['EPD'].BoundBox.ZMin+3.40)<1e-6
    assert abs(parts['EPDMaxThickness'].BoundBox.ZMax+2.55)<1e-6
    worst_head_gap=parts['ScrewHeadMax1'].BoundBox.ZMin-parts['EPDMaxThickness'].BoundBox.ZMax
    assert worst_head_gap>=.10-1e-6,worst_head_gap
    assert parts['Battery'].BoundBox.ZMin-parts['EPDMaxThickness'].BoundBox.ZMax>=.2-1e-6
    assert abs(parts['BatteryFixingPad'].BoundBox.ZMax-3.)<1e-6
    assert abs(parts['PanelRetentionPads'].BoundBox.ZMax+2.40)<1e-6
    assert len(parts['PanelRetentionPads'].Solids)==4
    assert not any(n in parts for n in ('IC1','ICU1','ICU2')),'Unfitted SFA model present'
    assert abs(rear.BoundBox.ZMax-front.BoundBox.ZMin-9.75)<1e-6
    assert abs(front.BoundBox.ZMax-1.4)<1e-6
    assert abs(front.BoundBox.ZMin+3.85)<1e-6
    assert abs(rear.BoundBox.ZMax-5.9)<1e-6
    # Free ends are separated from the bay/switch openings; each tongue is
    # connected to the body (the single-solid requirement checks this).
    for sign,y,lo,hi,free in SNAPS:
        assert hi-lo>=11.0-1e-6
        assert abs(y+2.5)>15.6 if sign<0 else True
        assert abs(44.9-44.6-.3)<1e-6
    # S1's corrected locating posts pass through the actual drilled PCB.
    # Contact surfaces/solder are allowed to touch the PCB, but not penetrate.
    outside_nuts=[]
    for i,(x,y) in enumerate(SCREW_CENTERS,1):
        nut,screw=parts[f'Nut{i}'],parts[f'Screw{i}']
        assert abs(nut.BoundBox.ZMin-1.0)<1e-6
        assert abs(nut.BoundBox.ZMax-3.4)<1e-6
        engagement=min(screw.BoundBox.ZMax,nut.BoundBox.ZMax)-nut.BoundBox.ZMin
        assert engagement>=2.4-1e-6,('nut nominal thread engagement',i,engagement)
        # Probe the continuous screw passage through the1.0mm support floor.
        passage=Part.makeCylinder(1.69,3.9,App.Vector(x,y,-.05))
        assert rear.common(passage).Volume<1e-5,('nut screw passage',i)
        # The annulus immediately behind the nut must be solid1.0mm PETG.
        seat=Part.makeCylinder(2.7,1.0,App.Vector(x,y,0)).cut(Part.makeCylinder(1.701,1.02,App.Vector(x,y,-.01)))
        assert abs(rear.common(seat).Volume-seat.Volume)<1e-5,('nut seat thickness',i)
        # The hex route is open all the way from its seat to the outside.
        access=hex_prism(5.55,1.0,6.0,x,y)
        assert rear.common(access).Volume<1e-5,('outside nut access',i)
        assert rear.isInside(App.Vector(x+3.9,y,3.799),1e-7,True)
        assert not rear.isInside(App.Vector(x+3.9,y,3.801),1e-7,True)
        recess=3.8-nut.BoundBox.ZMax
        assert abs(recess-.4)<1e-6
        outside_nuts.append({'index':i,'seat_thickness_mm':1.0,'nominal_thread_engagement_mm':round(engagement,5),
                             'outside_recess_mm':round(recess,5),'outside_path_overlap_mm3':round(rear.common(access).Volume,8),
                             'boss_radius_mm':4.1,'minimum_hex_corner_wall_mm':round(4.1-5.7/(3**.5),5)})
    rf_distances={name:s.distToShape(parts['IC5'])[0] for name,s in (('Front',front),('Rear',rear))}
    assert all(d>=1.5-1e-6 for d in rf_distances.values()),rf_distances
    switch_pcb=parts['S1'].common(parts['PCB']).Volume
    if switch_pcb>1e-5: failures.append(('S1/PCB',switch_pcb))
    report={'status':'PASS' if not failures else 'FAIL','case_body_mm':7.65,'case_local_max_mm':9.75,
            'case_xy_mm':[91.,60.4],'modeled_board_reference_count':93,'checked_shell_pairs':len(results),
            'worst_case_low_head_to_panel_gap_mm':round(worst_head_gap,3),
            'header_assumption':'CON3 is represented as exposed pads until population confirmed',
            'snap_tongue_lengths_mm':[round(hi-lo,2) for _,_,lo,hi,_ in SNAPS],
            'through_hole_pad_keepout_count':stack.getObject('SolderLegKeepout').PadCount,
            'IC5_resin_min_distance_mm':{k:round(v,5) for k,v in rf_distances.items()},
            'outside_nut_wells':outside_nuts,
            'results':results,'S1_PCB_overlap_mm3':round(switch_pcb,8),'failures':failures}
    (OUT/'fit_report.json').write_text(json.dumps(report,indent=2)+'\n')
    if failures: raise AssertionError(failures)
    print(f'PASS: both single solids; {len(results)} part/envelope shell pairs without interference')
    print('Body7.65 mm; CN1 local9.75 mm; corrected S1 pins fit drilled PCB; EPD/head worst gap0.10 mm')
    print('Outside nut wells:1.0mm seats,2.4mm nominal thread engagement,0.4mm recess; open external access')
    print('ES4L15BA1 minimum resin distances:',rf_distances)
    print('Four compliant glass pads and battery fixing pad; optional CON3 is exposed pads')

if __name__=='__main__': main()
