--- RAD_UtilMainms
-- Copyright (c) 2010 Sunside Inc., All Rights Reserved
-- Author: Joe Riedel
-- See Radiance/LICENSE for licensing terms.

utility RadianceUtil "Radiance Tools" width:166 height:600
(
	GroupBox grp1 "Selected Geo && Bones" pos:[6,83] width:155 height:427
	GroupBox grp4 "Animation" pos:[15,100] width:138 height:270
	dropdownList animList "" pos:[18,118] width:130 height:21
	edittext animName "Name:" pos:[18,146] width:130 height:17
	GroupBox grp8 "Export" pos:[6,512] width:154 height:79
	checkbox selectionOnlyCheckbox "Selection Only" pos:[69,527] width:87 height:14
	checkbox exportHiddenCheckbox "Hidden" pos:[11,527] width:55 height:14
	button exportSceneButton "Scene..." pos:[11,546] width:65 height:17
	button exportAnimButton "Anim..." pos:[89,569] width:65 height:17
	button exportMeshButton "Mesh..." pos:[89,546] width:65 height:17
	button exportSkelButton "Skel..." pos:[11,568] width:65 height:17
	edittext animStart "Start:" pos:[18,170] width:66 height:17
	edittext animEnd "End:" pos:[86,170] width:66 height:17
	checkbox animLoop "Looping" pos:[21,249] width:63 height:18
	dropdownList animInitialState "" pos:[22,289] width:123 height:21
	GroupBox grp32 "Initial State" pos:[19,271] width:129 height:64
	button animSetStart "Set Start" pos:[20,194] width:61 height:20
	button animSetEnd "Set End" pos:[87,194] width:61 height:20
	button animTest "Test" pos:[52,222] width:61 height:20
	checkbox animExport "Export" pos:[91,249] width:53 height:18
	button addAnim "Add" pos:[24,346] width:53 height:18
	button deleteAnim "Delete" pos:[87,346] width:53 height:18
	button noDefault "None" pos:[53,314] width:54 height:16
	checkbox cinematicObject "Cinematic Object" pos:[21,452] width:108 height:17
	checkbox hideUntilRef "Hide Until Referenced" pos:[21,469] width:129 height:17
	checkbox hideWhenDone "Hide When Done" pos:[21,487] width:129 height:17
	GroupBox grp5 "Selected Material" pos:[8,4] width:153 height:74
	button nameCompleteMapsButton "Autoname Complete Maps" pos:[14,49] width:138 height:22
	edittext autonamePackage "Package:" pos:[18,25] width:132 height:15
	GroupBox grp6 "" pos:[14,374] width:139 height:71
	edittext bboxSize "" pos:[24,395] width:113 height:17
	button calcBBoxSize "Calculate" pos:[38,420] width:86 height:17
	checkbox setBBox "Set BBox" pos:[25,373] width:64 height:15
	
	local animSel = #() -- selected objects that have animations
	local animSet = #() -- selected set of animations [struct Animation]
	
	persistent global RadianceUtil_lastSceneFile
	
	function exportScene doMeshes doAnims =
	(
		hidden  = exportHiddenCheckbox.checked
		objs = objects;
		oldSelection = selection
		oldRange = animationRange
		oldTime = sliderTime
		if (selectionOnlyCheckbox.checked) then ( objs = selection )
		objs = RAD_FilterVisible objs vis:(not hidden)		
		objs = RAD_ConvertableFilter objs #(Mesh, Freecamera)
		objs = RAD_Exclude objs #(Bone, CATBone, HubObject) -- exclude bones
		
		if (objs.count == 0) then
		(
			messagebox "No objects to export!"
			return undefined
		)
		
		if (RadianceUtil_lastSceneFile == undefined) then ( RadianceUtil_lastSceneFile = "Scene.3dx" )
		file = getSaveFileName caption:"Scene Export..." filename:RadianceUtil_lastSceneFile types:"3DX Files (*.3dx)|*.3dx|All Files (*.*)|*.*"
		if (file == undefined) then return undefined
		RadianceUtil_lastSceneFile = file
		
		disableSceneRedraw()
		RAD_ExportObjectsToSceneFile objs doMeshes doAnims file
		enableSceneRedraw()
		clearSelection()
		select oldSelection
		max utility mode
		animationRange = oldRange
		sliderTime = oldTime
	)
	
	function enableMeshControls val =
	(
		exportMeshButton.enabled =val 
		exportSkelButton.enabled = false 
	)
	
	function enableAnimControls val =
	(
		animList.enabled = val
		animStart.enabled = val
		animEnd.enabled = val
		animName.enabled = val
		animSetStart.enabled = val
		animSetEnd.enabled = val
		animTest.enabled = val
		animInitialState.enabled = val
		animLoop.enabled = val
		animExport.enabled = val
		addAnim.enabled = val
		deleteAnim.enabled = val
		noDefault.enabled = val
		exportAnimButton.enabled = val
	)
	
	function enableAnimObjControls val =
	(
		cinematicObject.enabled = val
		hideUntilRef.enabled = val
		hideWhenDone.enabled = val
	)
	
	function disableAll =
	(
		enableMeshControls false
		enableAnimControls false
		enableAnimObjControls false
	)
	
	function selectedMeshes = 
	(
		meshes = (RAD_ConvertableFilter (getCurrentSelection()) #(Mesh))
		RAD_Exclude meshes #(Bone, CATBone, HubObject) -- filter bones
	)
	
	function selectedAnimatable =
	(
		RAD_FilterAnimatable (getCurrentSelection())
	)
	
	function saveAnims =
	(
		for x in animSel do
		(
			for y in animSet do
			(
				RAD_SetObjAnim x y
			)
		)
		
		for x in animSel do
		(
			RAD_SetObjInitialAnim x animInitialState.selected
		)
	)
	
	function setupUIForSelAnim =
	(
		anim = animSet[animList.selection]
		animName.text = anim.name
		
		if (anim.start != undefined) then
		(
			animStart.text = anim.start as string
		)
		else
		(
			animStart.text = ""
		)
		
		if (anim.end != undefined) then
		(
			animEnd.text = anim.end as string
		)
		else
		(
			animEnd.text = ""
		)
		
		if (anim.loop != undefined) then
		(
			animLoop.checked = anim.loop
		)
		else
		(
			animLoop.triState = 2
		)
		
		if (anim.export != undefined) then
		(
			animExport.checked = anim.export
		)
		else
		(
			animExport.triState = 2
		)
	)
	
	function deleteSelAnim =
	(
		anim = animSet[animList.selection]
		initial = animInitialState.selected
		
		if (anim.name == initial) then 
		(
			for x in animSel do
			(
				RAD_SetObjInitialAnim x undefined
			)
		)
		
		for x in animSel do
		(
			RAD_DeleteObjAnim x animList.selected
		)
		
		deleteItem animSet animList.selection
		items = (for x in animSet collect x.name)
		animList.items = items
		animInitialState.items = items
		
		if (animSet.count > 0) then
		(
			animList.selection = 1
			animInitialState.selection = findItem animInitialState.items (RAD_FilterInitialAnim animSel)
			setupUIForSelAnim()
		)
		else
		(
			animName.text = ""
			animStart.text = ""
			animEnd.text = ""
			animLoop.checked = false
			animExport.checked = false
			enableAnimControls false
			addAnim.enabled = true
		)
	)
	
	function addNewAnim =
	(
		anim = Animation (RAD_NewAnimName animSet) 0 0 false true
		append animSet anim
		items = (for x in animSet collect x.name)
		animList.items = items
		animList.selection = 1
		animInitialState.items = items
		animInitialState.selection = findItem animInitialState.items (RAD_FilterInitialAnim animSel)
		enableAnimControls true
		setupUIForSelAnim()
		saveAnims()
	)
	
	function renameSelAnim name =
	(
		if (name == "") then
		(
			messagebox "Invalid animation name!"
			return undefined
		)
		
		c = animList.selection
		if (name != animSet[c].name) then
		(
			if ((RAD_FindAnimIndex animSet name) > 0) then
			(
				messagebox "An animation with that name already exists!"
				return undefined
			)
			
			for x in animSel do
			(
				RAD_RenameObjAnim x animSet[c].name name
			)
			
			animSet[c].name = name
			animList.items[c] = name
			animList.selected = name
			
			animInitialState.items[c] = name
			if (animInitialState.selection == c) then animInitialState.selected = name
		)
	)
	
	function setupUIForAnims objs =
	(
		-- get animatables in the selection
		animSel = RAD_FilterAnimatable objs
		animSet = RAD_GatherAnims animSel
		
		if (animSet.count > 0) then (
			items = (for x in animSet collect x.name)
			animList.items = items
			animList.selection = 1
			animInitialState.items = items
			animInitialState.selection = findItem animInitialState.items (RAD_FilterInitialAnim animSel)
			enableAnimControls true
			setupUIForSelAnim()
		)
		else if (animSel.count > 0) then (
			animList.items = #()
			animList.enabled = false
			animInitialState.items = #()
			animInitialState.enabled = false
			animStart.enabled = false
			animEnd.enabled = false
			animSetStart.enabled = false
			animSetEnd.enabled = false
			animLoop.enabled = false
			animExport.enabled = false
			noDefault.enabled = false
			addAnim.enabled = true
			deleteAnim.enabled = false
		)
		
		bboxEnabled = (animSel.count > 0)
		setBBox.enabled = bboxEnabled
		
		if (bboxEnabled) then (
			bboxEnabled = false
			vals = RAD_GatherObjectProps animSel #("SetBBox") #("false")
			if (vals[1]) then (
				bboxEnabled = vals[2][1][2][1] as BooleanClass
				if (bboxEnabled) then (
					setBBox.triState = 1
				) else (
					setBBox.triState = 0
				)
			) else (
				-- undefined
				setBBox.state = 2
			)
			
			if (bboxEnabled) then (
				vals = RAD_GatherObjectProps animSel #("BBox") #("-24 -24 -24 24 24 24")
				if (vals[1]) then (
					bboxSize.text = vals[2][1][2][1]
				) else (
					bboxSize.text = ""
				)
			)			
		)
		
		if (not bboxEnabled) then (
			bboxSize.enabled = false
			calcBBoxSize.enabled = false
			bboxSize.text = ""
		)
	)
	
	function setupUIForAnimatable objs = 
	(
		setupUIForAnims objs
		
		enableAnimObjControls true
		
		vals = RAD_GatherObjectProps objs #("CinematicObject") #("false")
		if (vals[1]) then
		(
			if (vals[2][1][2][1] as BooleanClass) then
			(
				cinematicObject.triState = 1 -- checked
			)
			else
			(
				cinematicObject.triState = 0
			)
		)
		else
		(
			cinematicObject.state = 2 -- undefined
		)
		
		vals = RAD_GatherObjectProps objs #("HideUntilRef") #("true")
		if (vals[1]) then
		(
			if (vals[2][1][2][1] as BooleanClass) then
			(
				hideUntilRef.triState = 1 -- checked
			)
			else
			(
				hideUntilRef.triState = 0
			)
		)
		else
		(
			hideUntilRef.state = 2 -- undefined
		)
		
		vals = RAD_GatherObjectProps objs #("HideWhenDone") #("true")
		if (vals[1]) then
		(
			if (vals[2][1][2][1] as BooleanClass) then
			(
				hideWhenDone.triState = 1 -- checked
			)
			else
			(
				hideWhenDone.triState = 0
			)
		)
		else
		(
			hideWhenDone.state = 2 -- undefined
		)
	)
	
	function setupUIForObjects objs =
	(
		meshes = RAD_ConvertableFilter objs #(Mesh)
		
		if meshes.count > 0 then
		(
			enableMeshControls true
		)
		
		setupUIForAnimatable objs
	)
			
	function onSelChange =
	(
		sel = RAD_FilterAnimatable selection --RAD_Exclude (RAD_FilterAnimatable selection) #(Bone, CATBone, HubObject)
		if (sel.count > 0) then
		(
			setupUIForObjects sel
		)
		else
		(
			animSel = #()
			animSet = #()
			animList.items = #()
			animInitialState.items = #()
			animName.text = ""
			animStart.text = ""
			animEnd.text = ""
			animLoop.checked = false
			animExport.checked = false
			bboxSize.text = ""
			disableAll()
		)
	)
	
	function fileOpen =
	(
	)
		
	on RadianceUtil open do
	(
		RadianceUtil.disableAll()
		RadianceUtil.onSelChange()
		RAD_AddSelChangeHandler "RadianceUtil.onSelChange()" #RadianceUtil_SELCHANGE
		RAD_AddFileOpenHandler "RadianceUtil.fileOpen()" #RadianceUtil_FILEOPEN
	)
	on RadianceUtil close do
	(
		RAD_RemoveSelChangeHandler #RadianceUtil_SELCHANGE
		RAD_RemoveFileOpenHandler #RadianceUtil_FILEOPEN
	)
	on animList selected idx do
	(
		animName.text = animSet[animList.selection].name
		animStart.text = animSet[animList.selection].start as string
		animEnd.text = animSet[animList.selection].end as string
		animExport.checked = animSet[animList.selection].export
		animLoop.checked = animSet[animList.selection].loop
	)
	on animName entered newVal do
	(
		renameSelAnim newVal
	)
	on exportSceneButton pressed do
	(
		exportScene true true
	)
	on exportAnimButton pressed do
	(
		exportScene false true
	)
	on exportMeshButton pressed do
	(
		exportScene true false
	)
	on exportSkelButton pressed do
	(
		exportScene false false
	)
	on animStart entered newVal do
	(
		if (newVal == "") then
		(
			messagebox "Please enter a number for the animation start frame."
			return undefined
		)
		
		animSet[animList.selection].start = (newVal as integer)
		saveAnims()
	)
	on animEnd entered newVal do
	(
		if (newVal == "") then
		(
			messagebox "Please enter a number for the animation end frame."
			return undefined
		)
		
		animSet[animList.selection].end = (newVal as integer)
		saveAnims()
	)
	on animLoop changed state do
	(
		animSet[animList.selection].loop = state
		saveAnims()
	)
	on animInitialState selected idx do
	(
		saveAnims()
	)
	on animSetStart pressed do
		(
	)
	on animSetEnd pressed do
		(
	)
	on animTest pressed do
		(
	)
	on animExport changed state do
	(
		animSet[animList.selection].export = state
		saveAnims()
	)
	on addAnim pressed do
	(
		addNewAnim()
	)
	on deleteAnim pressed do
	(
		deleteSelAnim()
	)
	on noDefault pressed do
	(
		animInitialState.selection = -1
		animInitialState.items = (for x in animSet collect x.name)
				
		for x in animSel do
		(
			RAD_SetObjInitialAnim x undefined
		)
	)
	on cinematicObject changed state do
	(
		if (state) then
		(
			RAD_SetObjectProps (selectedAnimatable()) #(#("CinematicObject", "true"))
		)
		else
		(
			RAD_SetObjectProps (selectedAnimatable()) #(#("CinematicObject", "false"))
		)
	)
	on hideUntilRef changed state do
	(
		if (state) then
		(
			RAD_SetObjectProps (selectedAnimatable()) #(#("HideUntilRef", "true"))
		)
		else
		(
			RAD_SetObjectProps (selectedAnimatable()) #(#("HideUntilRef", "false"))
		)
	)
	on hideWhenDone changed state do
	(
		if (state) then
		(
			RAD_SetObjectProps (selectedAnimatable()) #(#("HideWhenDone", "true"))
		)
		else
		(
			RAD_SetObjectProps (selectedAnimatable()) #(#("HideWhenDone", "false"))
		)
	)
	on nameCompleteMapsButton pressed do
	(
		mat = meditMaterials[activeMeditSlot]
		
		if (classof(mat) != MultiMaterial) then (
			if (classof(mat) != StandardMaterial) then (
				messagebox "Autoname Failed: The material selected in the Material Editor is not supported by the autorename feature!"
				return undefined
			)
			
			diffuseBitmap = mat.maps[2]
			if (diffuseBitmap == undefined) then (
				print ("WARNING: Material '" + mat.name + "' has no diffuse map... ignored.")
				continue
			)
			x = getFilenameFile diffuseBitmap.filename
			mat.name = autonamePackage.text + "/" + x + "_M"
			
			return undefined
		)
		
		for sub in mat.materialList do (
			if (classof(sub) != StandardMaterial) then (
				print ("WARNING: Multi/Sub-Object material '" + mat.name + "' sub-material '" + sub.name + "' is not a standard material... ignored.")
				continue
			)
			
			diffuseBitmap = sub.maps[2]
			if (diffuseBitmap == undefined) then (
				print ("WARNING: Multi/Sub-Object material '" + mat.name + "' sub-material '" + sub.name + "' has no diffuse map... ignored.")
				continue
			)
			x = getFilenameFile diffuseBitmap.filename
			sub.name = autonamePackage.text + "/" + x + "_M"
		)
	)
	on setBBoxSize changed state do (
		if (state) then (
			RAD_SetObjectProps (selectedAnimatable()) #(#("SetBBox", "true"))
		) else (
			RAD_SetObjectProps (selectedAnimatable()) #(#("SetBBox", "false"))
		)
	)
	on bboxSize entered newVal do
	(
		RAD_SetObjectProps (selectedAnimatable()) #(#("BBox", newVal))
	)
	on calcBBoxSize pressed do
	(
		objs = selectedAnimatable()
		for i=1 to objs.count do (
			bbox = RAD_MeshBBox (snapshotAsMesh objs[i])
			s = ""
			for k=1 to 2 do (
				for z=1 to 3 do (
					s = s + (bbox[k][z] as string) + " "
				)
			)
			RAD_SetObjectProps (#(objs[i])) #(#("BBox", s))
		)
		
		vals = RAD_GatherObjectProps objs #("BBox") #("-24 -24 -24 24 24 24")
		if ((animSel.count > 0) and vals[1]) then (
			bboxSize.text = vals[2][1][2][1]
		) else (
			bboxSize.text = ""
		)
	)
)