# Original ordinary actor node bounds

DefaultFactory root and container nodes inherit the actual CEmptySceneNode local
box: minimum (-1,-1,-1), maximum (1,1,1). This is per-node storage initialized by
the original constructor at 0x2587cd and returned at 0x2587c5. It does not aggregate
child geometry. Ordinary mesh-node local bounds at 0x2a2685 delegate to their
actual constructed mesh's authored box. Skin-node local bounds keep their
existing dynamic mesh getter and shared palette/bounds dirty behavior.

For ordinary nodes and these containers, getTransformedBoundingBox at 0x272b05
checks only flag0x200. A clean call returns the retained transformed cache.
A dirty call obtains the actual local box, transforms using the CURRENT CACHED
absolute matrix, stores the result and clears only0x200. It never calls relative
or absolute transform update. Local TRS setters alone can therefore leave an
older transformed box visible. Visibility does not suppress these getters.
The distinct local and transformed storage identities stay stable.

The call resolves to CMatrix4::transformBoxEx at0x243cad, not the skin routine
transformBox at0x2b1325. The Ex routine initializes both output corners from the
translation, then for each output axis visits input X,Y,Z. It multiplies each
coefficient by both authored corner values, adds the smaller product to minimum
and larger to maximum, using a strict less-than comparison. It does not take an
identity shortcut, use homogeneous matrix entries, or transform only two
opposite corners. Its translation-first arithmetic and tie branch are retained
for float rounding and signed zeros. The existing skin algorithm remains
separate because its behavior is different; no false consolidation is applied.

`retailSceneTransformBoxEx` implements that one authoritative ordinary-node
operation. Nonfinite addressed inputs reject explicitly; unused homogeneous
entries are not read. Containers and mesh nodes use it through their actual
getter. The global CSceneManager-specific local box, custom node kinds, LOD,
camera/culling and full physics/renderer host integration remain separate
domains. No child aggregation or guessed global-manager box is substituted.

Validation executes original local/transformed getters, transformBoxEx and
factory/TRS/cache/frame methods for both supplied Player models: nine phases
each, all35 non-skin nodes, 630 snapshots. Another21 direct original Ex cases
cover shear, negative scale, fractional rounding, signed zeros, inverted authored
corners and ignored homogeneous NaNs. All19,026 state/component words match in
Release and Debug. The original reference retains reviewed zero-buffer geometry
isolation; it does not claim complete original material/driver construction or
GPU parity. Existing skin/pose/global-ownership regressions remain required.
