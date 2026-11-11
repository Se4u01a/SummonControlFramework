Scriptname SCF
{Summon Control Framework - the script registration API.

Use this to hand SCF an actor the engine does not report as one of the player's
commanded actors: a scripted creature, a modded summon, a custom conjuration. A
registered actor is treated like a vanilla summon - SCF's orders, follow / move-to
movement, combat policy, friendly-fire protection, collision rules and the minion
HUD all apply to it.

The registration is a flag on the SCF record, not a spell effect: it lasts until
UnregisterSummon, the actor's death, or the actor's form disappearing. It does not
take a conjuration slot, is never displaced by the summon limit, and is never
dropped for "the engine roster does not list it".

Only the player can be the owner. Passing no owner means the player.}

; Registers akSummon as a summon of akOwner and returns true when SCF tracks it.
; False when an actor argument is missing, akSummon is akOwner, akSummon is dead,
; or the owner is not the player.
bool Function RegisterSummon(Actor akSummon, Actor akOwner = None) global native

; Releases akSummon from SCF. The actor keeps existing; SCF simply lets go of it.
; False when SCF was not tracking it.
; An actor the engine still lists among the player's commanded summons is re-registered
; on the next tactical tick (the engine, not the script, owns that record).
bool Function UnregisterSummon(Actor akSummon) global native

; True while SCF tracks akSummon as an active summon (record present, actor alive).
bool Function IsRegistered(Actor akSummon) global native

; Every live summon SCF tracks for akOwner (the player when akOwner is None),
; ordered by FormID.
Actor[] Function GetSummons(Actor akOwner = None) global native

; Number of summons SCF tracks for akOwner (the player when akOwner is None).
int Function GetSummonCount(Actor akOwner = None) global native
