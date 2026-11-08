# Editor MME examples (CC0-1.0)

Assign `Material.fx` to a model or one material. `Tint` is an editable float4.
Assign `Scene.fx` to an accessory as a scene carrier; its two fullscreen passes
copy the complete scene and apply `Exposure`, preserving alpha. Both are ordinary
FX9/SAS, use the adjacent `Common.fxh`, and require no NG-specific annotation.

The editor saves relative references and numeric overrides in `project.pmm.ng-effects`.
Keep this file beside the PMM when copying a project. The effects and includes must
also remain available. Unsupported effects report an error; **Use basic materials**
is an explicit user choice. These examples do not imply full FX9 or Ray support.
