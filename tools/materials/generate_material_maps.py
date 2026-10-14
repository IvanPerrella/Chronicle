#!/usr/bin/env python3
"""Rebuild original deterministic material maps using only the standard library.

No retail image is read, sampled or redistributed. Atlas regions are independently
authored UV annotations in a 256-unit coordinate system. Height fields are analytic.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "assets" / "materials"
SIZE = 128
# Stable semantic IDs; roughness, metalness, amplitude, frequency, characteristic.
PROFILES = {
    "wood_floor": (0.80, 0., .105, 47, "grain"),
    "wood_wall": (.84, 0., .08, 38, "grain"),
    "cloth": (.94, 0., .055, 42, "weave"),
    "leather": (.86, 0., .085, 21, "pebble"),
    "skin": (.94, 0., .015, 27, "pebble"),
    "metal": (.43, .92, .055, 34, "brushed"),
    "stone": (.88, 0., .20, 11, "stone"),
    "prop": (.86, 0., .08, 18, "pebble"),
    "flat": (1., 0., 0., 1, "flat"),
    "grass": (.98, 0., .09, 32, "grass"),
    "moss": (.98, 0., .12, 25, "pebble"),
    "effect": (1., 0., 0., 1, "flat"),
}
IDS = {name: i for i, name in enumerate(PROFILES)}
# Surface finishes change microstructure and conductivity without changing the
# underlying semantic material. Oxide and paint cover the conductor beneath.
FINISHES = {
    "oxidized_iron": (.88, .10, .07, 23, "corrosion"),
    "worn_gold": (.64, .82, .035, 21, "pebble"),
}
ATLAS_FINISHES = {"d03b07": "oxidized_iron"}
# Observed source atlas dimensions; shared procedural detail may tile at 128Â².
DIMENSIONS = {
    "alpha01":(64,64), "ankfnt24":(128,128), "awabres":(128,128),
    "basefx00":(128,128), "basefx01":(128,128), "bomb_ex":(256,128),
    "c01d11":(128,192), "c01w00":(128,128), "c01w01":(128,128),
    "ceffe_s":(128,128), "d00e01":(128,128), "d00e03":(128,128),
    "d00e03_a":(128,128), "d01e03":(128,192), "d01e04":(64,64),
    "d01e05":(64,64), "d03b13":(64,64), "d04e01":(128,128),
    "dunenter":(512,384), "e23a02":(128,128), "e28a02":(128,128),
    "e52a02":(128,128), "e52a03":(128,128), "e67a02":(128,128),
    "floor02":(384,192), "floor05":(384,192), "frame":(640,448),
    "fuki256":(128,128), "fuki256b":(128,128), "fx_foot":(32,32),
    "fx_foot2":(32,32), "gaiji":(384,256), "gatekey03":(128,256),
    "gatekey06":(128,256), "iteme01":(64,64), "iteme02":(64,64),
    "itemicon":(256,512), "itempack":(256,192), "lightling":(64,64),
    "maseki_ex1":(128,128), "maseki_ex2":(128,128), "maseki_ex3":(128,128),
    "maseki_ex4":(128,128), "minimap1":(128,192), "nebaneba":(128,128),
    "pause":(128,64), "stayframe":(640,224), "syst04":(16,16),
    "wepicon":(256,640), "wepready":(128,128),
}


def rect(material, x0, y0, x1, y1):
    return {"material": material, "rect": [x0, y0, x1, y1]}


def polygon(material, *points):
    return {"material": material, "polygon": list(points)}


def finish_rect(material, finish, x0, y0, x1, y1):
    return {**rect(material,x0,y0,x1,y1), "finish":finish}


# Half-open rectangles. Later annotations override earlier annotations.
ATLASES = {
    "d03b01": ("wood_floor", []),
    "d03b02": ("wood_wall", []),
    "d03b03": ("wood_wall", []),
    "d03b04": ("wood_floor", []),
    "d03b05": ("wood_wall", [rect("metal",0,0,128,256), rect("cloth",128,0,192,128),
        rect("metal",192,0,256,72), rect("wood_wall",0,32,128,96), rect("wood_wall",128,128,256,256),
        rect("metal",128,160,192,192)]),
    "d03b06": ("flat", [polygon("cloth",(0,0),(172,0),(256,62),(167,128),(0,121)),
        rect("wood_wall",0,128,256,192), polygon("cloth",(0,250),(80,215),(164,205),(256,245),(256,256),(0,256))]),
    "d03b07": ("metal", [finish_rect("metal","worn_gold",128,32,256,96),
        rect("effect",128,128,256,256),
        polygon("metal",(128,224),(132,208),(144,196),(160,192),(176,196),(188,208),
            (192,224),(188,240),(176,252),(160,256),(144,252),(132,240))]),
    "d03b08": ("wood_wall", [rect("metal",0,128,256,256)]),
    "d03b10": ("metal", [rect("cloth",0,64,256,88), rect("stone",0,128,128,256),
        rect("effect",192,128,256,192), rect("effect",128,192,256,256)]),
    "d03b11": ("wood_wall", [rect("metal",0,64,128,84),rect("metal",224,0,240,256),rect("metal",128,120,256,136)]),
    "d03b14": ("effect", [rect("metal",0,192,128,224),rect("wood_wall",0,224,256,256)]),
    "d03v01": ("skin", [rect("cloth",0,0,64,128), rect("flat",64,0,128,32),
        rect("wood_wall",64,32,128,64),rect("flat",64,64,128,128),rect("effect",128,0,192,128),
        rect("leather",0,128,256,256)]),
    "d03v02": ("skin", [rect("metal",64,0,256,64),rect("cloth",64,64,256,256)]),
    "d06b01": ("stone", []),
    "d06b02": ("flat", [polygon("cloth",(0,70),(100,0),(150,38),(175,120),(160,230),(65,225),(0,160)),rect("stone",192,0,256,256)]),
    "d06b06": ("stone", [rect("flat",128,0,256,128),polygon("cloth",(128,0),(190,0),(205,40),(176,64),(128,32)),
        polygon("cloth",(145,80),(185,74),(220,96),(200,128),(128,128),(128,100))]),
    "d06b10": ("stone", [polygon("metal",(64,8),(192,8),(248,64),(248,192),(192,248),(64,248),(8,192),(8,64))]),
    "d06b05": ("stone", []),
    "d06b09": ("stone", []),
    "d06b11": ("flat", []),
    "d06b12": ("stone", [rect("effect",48,0,88,96),rect("effect",0,128,128,192),rect("flat",128,0,256,128)]),
    "d06b13": ("stone", []),
    "d06b15": ("stone", [rect("flat",32,0,64,48),rect("flat",224,0,256,64),rect("flat",160,128,192,256)]),
    "d06v01": ("cloth", [rect("metal",0,0,256,12),rect("metal",0,244,256,256),rect("metal",0,0,12,256),rect("metal",244,0,256,256)]),
    "d01t05": ("wood_wall", [rect("metal",0,0,256,128),rect("metal",0,128,16,256),rect("metal",112,128,144,256),rect("metal",240,128,256,256)]),
    "e23a01": ("skin", []),
    "e23a02": ("skin", [rect("cloth",0,0,128,128),rect("leather",128,96,224,256),rect("flat",224,96,256,256)]),
    "e28a01": ("cloth", [rect("metal",0,0,256,128),rect("skin",0,128,128,256),rect("flat",192,224,256,256)]),
    "e28a02": ("skin", [rect("cloth",0,0,256,64),rect("flat",128,64,192,128)]),
    "e52a01": ("cloth", [rect("flat",0,128,64,192),rect("leather",192,0,256,128),
        rect("metal",80,100,128,128),rect("metal",170,112,224,172),rect("metal",192,208,256,256)]),
    "e52a02": ("cloth", [rect("metal",0,0,20,256),rect("metal",80,80,104,104)]),
    "e52a03": ("cloth", [rect("skin",0,0,64,96),rect("skin",128,128,256,256)]),
    "e61a01": ("skin", []),
    "e67a01": ("skin", [rect("effect",0,208,64,256)]),
    "e67a02": ("skin", []),
    "gatekey03": ("cloth", [rect("metal",0,0,128,256)]),
    "gatekey06": ("cloth", [rect("metal",0,0,128,256)]),
    "c01d02": ("cloth", [rect("leather", 0,16,128,128), rect("skin",0,0,128,32),
        rect("skin",64,32,96,64), rect("metal",96,32,128,64),
        rect("skin",128,128,256,256), rect("cloth",0,128,128,256), rect("cloth",192,160,224,192)]),
    "c01d03": ("leather", [rect("cloth",0,64,64,128), rect("skin",64,64,128,128),
        rect("leather",128,0,256,128), rect("metal",224,0,256,64),
        rect("metal",192,32,224,64), rect("cloth",64,192,128,256)]),
    "c01w00": ("metal", [rect("leather",128,32,192,224)]),
    "c01w01": ("metal", [rect("leather",128,32,192,224)]),

}
# Sparse organic seam annotations on the cracked Gallery floor.
MOSS_ISLANDS = {"d06b05": [(23, 75, 6, 17), (172, 155, 8, 12), (239, 221, 7, 13)]}
WET_ATLASES = {"d06b05", "d06b09"}
# Runtime upward-facing strips use this exact right-hand floor tile island.
WET_REGIONS = {"d06b01": [rect("stone",192,0,256,256)]}
# FNV-1a64 over LE u32 width, LE u32 height, then decoded row-major RGBA.
# Main/inter hashes are equal across all their MPD packs. Decoded samples also
# match the transformed runtime texture dump byte-for-byte.
VARIANT_HASHES = {
    "d03b01": ("8642a0965966131a","87882a9739850763"),
    "d03b02": ("167dcf9d191fe58c","86d2985ce37615f6"),
    "d03b03": ("3e7e0ab7c5292744","c75e84f615421063"),
    "d03b04": ("ee1be3c9b5ab911f","433c47aff0320b63"),
    "d03b05": ("16895bcb66000401","081520c441face06"),
    "d03b06": ("5bcd9e2cf25fa579","277e3b72530f9915"),
    "d03b07": ("bb52e83ce80b09d9","8ec25ed9eb04362e"),
    "d03b08": ("b200abae2cbc70be","94f3b0d4310e2d7f"),
    "d03b09": ("76e23b1240f5acb9","bb6b6b9801577c1d"),
    "d03b10": ("2b701d71e7144f81","ef65df927f3dd2a1"),
    "d03b11": ("f1da673224dd094b","7baa4c1c08e33379"),
    "d03b12": ("86f336035f3afe53","1de23ced9e4536d4"),
    "d03b13": ("2c211d7704e79e5a","24331c44e46cb46e"),
    "d03b14": ("6f7a3689c2e68ece","d67128ad84a77cd9"),
}
ATLASES.update({
    "d03boss_b01": ("stone",[rect("flat",128,0,192,64)]),
    "d03boss_b08": ("stone",[rect("metal",0,128,256,256)]),
    "d03boss_b12": ("stone",[polygon("metal",(100,16),(144,8),(160,56),(132,80),(100,64))]),
    "d03boss_b13": ("stone",[rect("cloth",64,0,192,256)]),
    "c13a02": ("skin",[rect("metal",16,24,48,128),rect("metal",64,24,96,128),rect("cloth",0,128,256,256)]),
    "c13a03": ("cloth",[rect("metal",0,0,64,96),rect("metal",0,128,64,256),rect("skin",0,96,64,128)]),
    "c13a04": ("cloth",[rect("skin",128,0,256,128)]),
    "c17a01": ("skin",[rect("leather",0,0,128,128),rect("leather",0,128,64,192)]),
    "c23a02": ("skin",[rect("effect",128,80,224,160),rect("flat",128,160,224,256)]),
    "c23a03": ("metal",[rect("cloth",0,0,64,256)]),
    "c23a04": ("metal",[rect("effect",0,0,256,96)]),
    "e117a01": ("skin",[rect("cloth",0,128,128,256),rect("metal",128,0,256,64)]),
    "e117a02": ("skin",[rect("effect",64,0,128,128),rect("leather",192,0,256,256)]),
    "e24a01": ("skin",[rect("leather",128,128,256,256)]),
    "e24a03": ("skin",[rect("cloth",0,192,64,256)]),
    "e25a01": ("metal",[rect("cloth",0,0,256,64)]),
    "e25a02": ("metal",[rect("wood_wall",128,0,224,128),rect("cloth",0,128,128,256),rect("wood_wall",128,128,256,256)]),
    "e26a01": ("skin",[rect("leather",0,0,128,128)]),
    "e27a01": ("skin",[rect("flat",0,0,256,96),rect("effect",0,192,64,256)]),
    "e27a02": ("skin",[rect("cloth",0,0,64,96),rect("metal",128,0,192,128),rect("cloth",64,128,128,192),rect("metal",224,192,256,256)]),
    "e34a01": ("metal",[rect("wood_wall",32,32,100,100),rect("wood_wall",144,32,224,100),rect("wood_wall",32,152,100,224),rect("wood_wall",144,152,224,224)]),
    "e34a02": ("metal",[rect("wood_wall",32,32,100,100),rect("wood_wall",32,152,100,224),rect("wood_wall",128,0,256,64)]),
    "e34a03": ("skin",[rect("cloth",0,128,128,256)]),
    "e34a04": ("skin",[rect("effect",0,128,256,256)]),
    "e35a01": ("flat",[rect("metal",0,128,256,256),rect("wood_wall",16,144,96,208),rect("wood_wall",144,144,224,208)]),
    "e35a02": ("skin",[rect("effect",0,128,128,256),rect("cloth",128,128,256,256)]),
    "e43a01": ("metal",[rect("cloth",0,128,128,256),rect("leather",64,0,160,128)]),
    "e43a02": ("metal",[rect("skin",96,64,128,160)]),
    "e51a01": ("cloth",[rect("skin",0,64,256,192),rect("metal",0,0,96,64)]),
    "e63a01": ("cloth",[rect("metal",0,0,128,64),rect("metal",0,128,256,192),rect("skin",128,64,256,128)]),
    "e63a02": ("skin",[rect("metal",128,128,256,256)]),
    "e65a01": ("leather",[rect("skin",0,0,128,64),rect("flat",224,0,256,64)]),
    "e68a01": ("skin",[rect("cloth",0,128,128,256),rect("effect",0,0,32,48)]),
    "e69a01": ("cloth",[rect("skin",128,0,192,128)]),
    "e74a01": ("skin",[rect("cloth",0,0,128,64)]),
    "e74a02": ("skin",[rect("effect",128,64,256,128)]),
    "e75a01": ("cloth",[rect("skin",0,128,128,256),rect("leather",128,128,192,256),rect("skin",192,128,256,256),rect("flat",192,0,256,128)]),
    "e75a02": ("cloth",[rect("grass",0,0,64,64),rect("metal",192,0,256,256)]),
    "e86a01": ("stone",[rect("flat",64,0,128,32),rect("skin",128,128,192,192)]),
    "e86a02": ("stone",[rect("skin",0,0,128,192)]),
})
for _card in ("e44","e45","e46","e47","e48"):
    ATLASES[_card+"a01"] = ("cloth",[rect("metal",112,48,208,192)])
    ATLASES[_card+"a02"] = ("cloth",[rect("skin",192,0,256,96),rect("effect",0,96,192,192)])

EXTRA_ENEMIES = {
    "skin": "c13a01 c17a02 c17a02_an c17a03 c17a03_an c17a04 c17a04_an c17a05 c23a01 c23a05 e24a02 e65a02".split(),
    "stone": "e55a01 e55a02".split(),
    "flat": "etoranp1 etoranp2".split(),
    "effect": "i_tatumaki b3_reiki baria c17_hikari i_boll i_meteo kouri last_gw last_mc smajin_beem smajin_beem_s smajin_kaze smajin_syougeki".split(),
}
ENEMY_128 = set("c13a01 c17a02 c17a03 c17a04 e117a02 e24a02 e24a03 e25a01 e26a02 e27a01 e34a01 e34a02 e34a03 e34a04 e35a01 e35a02 e43a02 e44a01 e44a02 e45a01 e45a02 e46a01 e46a02 e47a01 e47a02 e48a01 e48a02 e55a02 e63a02 e65a02 e74a02 e75a02 e86a02 etoranp1 etoranp2".split())
PARTY = {
    "c04b01": ("skin",[]),
    "c04b02": ("skin",[rect("leather",0,0,64,80),rect("leather",128,0,256,96),rect("metal",0,80,64,144),rect("cloth",0,144,128,224),rect("leather",0,224,256,256)]),
    "c04b03": ("leather",[rect("skin",128,0,192,64),rect("cloth",0,128,64,192)]),
    "c04b04": ("cloth",[rect("leather",0,128,128,256)]),
    "c05a01": ("skin",[]),
    "c05a02": ("skin",[rect("leather",0,128,256,256)]),
    "c05a03": ("cloth",[rect("metal",192,48,256,80),rect("metal",224,128,256,224)]),
    "c05a04": ("cloth",[rect("leather",0,0,32,256)]),
    "c06a01": ("skin",[]),
    "c06a02": ("leather",[]),
    "c06a03": ("leather",[rect("skin",0,96,128,160),rect("cloth",0,160,128,224),rect("skin",0,224,256,256),rect("metal",128,64,192,128)]),
    "c10a01": ("skin",[]),
    "c10a02": ("cloth",[rect("skin",128,0,192,128),rect("leather",128,128,256,256),rect("metal",224,192,256,256)]),
    "c10a03": ("leather",[rect("skin",0,128,128,256),rect("flat",192,0,256,128),rect("metal",192,208,256,256)]),
    "c18a01": ("metal",[rect("cloth",0,0,64,128),rect("leather",128,0,256,128),rect("cloth",64,208,192,256)]),
    "c18a02": ("leather",[rect("skin",0,0,128,128),rect("metal",128,0,224,64),rect("cloth",0,128,256,256)]),
    "c18a03": ("cloth",[rect("metal",0,0,128,64),rect("metal",192,0,256,256)]),
    "c18a04": ("cloth",[]),
}
ATLASES.update(PARTY)

# Inspected weapon atlas material islands. This enumerates exact retail entries;
# it is not a prefix resolver or an unexamined material fallback.
WEAPONS = {}
def weapon(name,profile="metal",regions=()):
    WEAPONS[name] = (profile,list(regions))

for _id,_bounds in {
    "02":(128,112,256,256),"03":(176,48,256,224),"04":(128,32,224,152),
    "05":(176,48,224,256),"06":(128,0,256,128),"07":(128,0,176,192),
    "09":(128,128,176,256),"10":(192,0,224,192),"13":(160,96,192,224),
    "14":(0,0,96,256),"16":(48,0,128,64),"18":(176,0,224,64),
    "19":(160,32,176,96),"25":(128,48,160,224),"26":(192,32,256,128),
    "28":(192,64,224,192),"29":(128,128,192,256),"30":(128,64,224,256),
    "31":(128,0,224,128),"32":(144,96,192,192),"33":(96,96,160,128),
    "34":(128,0,176,96),"36":(128,48,192,176),"37":(128,96,160,160),
    "38":(160,32,192,192),"39":(160,64,224,256),"40":(192,128,256,192),
    "41":(192,128,256,192),
}.items():
    weapon("c01w"+_id,regions=[rect("leather",*_bounds)])
weapon("c01w08","metal",[rect("wood_wall",192,0,256,256)])
weapon("c01w11","metal",[rect("grass",0,0,256,96)])
weapon("c01w12","skin",[rect("metal",0,0,64,256),rect("cloth",64,192,128,224)])
weapon("c01w15a","wood_wall",[rect("skin",64,192,192,256),rect("cloth",96,64,192,160)])
weapon("c01w15b","wood_wall",[rect("skin",128,0,256,128),rect("skin",128,192,256,256)])
weapon("c01w17","wood_wall",[rect("metal",128,0,256,256),rect("cloth",0,32,96,224)])
weapon("c01w35","wood_wall",[rect("cloth",192,128,256,256)])
for _id in ("21","22","23","24","27"):
    weapon("c01w"+_id)
for _id in ("00","01","02","03","04","05","06","07","08","09","10","12","13","14"):
    weapon("c04w"+_id,"wood_wall",[rect("cloth",112,0,176,128),rect("leather",0,128,256,256)])
WEAPONS["c04w02"] = ("leather",[rect("cloth",128,0,224,128)])
WEAPONS["c04w03"] = ("wood_wall",[rect("cloth",128,0,192,160),rect("metal",128,160,192,192)])
WEAPONS["c04w06"] = ("cloth",[rect("metal",0,0,256,48),rect("metal",0,176,112,256)])
WEAPONS["c04w07"] = ("metal",[rect("cloth",128,0,224,96),rect("leather",0,160,256,224)])
WEAPONS["c04w08"] = ("leather",[rect("skin",0,0,256,112),rect("metal",0,192,256,256)])
WEAPONS["c04w14"] = ("metal",[rect("cloth",0,48,128,256)])
for _id in ("00","01","02","03","04","05","06","07","08","09","10","12","13","14"):
    weapon("c05w"+_id)
WEAPONS["c05w03"] = ("effect",[])
WEAPONS["c05w05"] = ("stone",[rect("metal",128,0,256,48)])
WEAPONS["c05w06"] = ("metal",[rect("effect",128,0,256,256)])
WEAPONS["c05w07"] = ("effect",[rect("metal",0,0,192,64)])
WEAPONS["c05w09"] = ("stone",[rect("skin",0,192,192,256)])
WEAPONS["c05w13"] = ("wood_wall",[rect("grass",0,0,128,128)])
WEAPONS["c05w14"] = ("metal",[rect("effect",128,0,256,256)])
for _id in ("00","01"):
    weapon("c06w"+_id,"wood_wall",[rect("cloth",0,0,128,256)])
weapon("c06w02","stone",[rect("cloth",192,0,256,128)])
weapon("c06w03","metal",[rect("wood_wall",176,64,256,224),rect("effect",0,0,128,256)])
weapon("c06w04","metal",[rect("leather",192,0,256,128),rect("cloth",0,0,128,256)])
weapon("c06w05","wood_wall",[rect("cloth",0,0,128,192)])
weapon("c06w06","metal",[rect("cloth",192,128,256,256)])
weapon("c06w07","stone")
weapon("c06w08","wood_wall",[rect("leather",64,0,128,64)])
weapon("c06w09","metal",[rect("cloth",128,48,256,96),rect("effect",0,0,64,96)])
weapon("c06w10","metal",[rect("cloth",64,128,128,256)])
weapon("c06w11","skin",[rect("cloth",128,0,192,128),rect("metal",192,128,256,256)])
weapon("c06w13")
weapon("c06w14","wood_wall",[rect("cloth",0,128,128,256),rect("metal",128,0,256,64)])
weapon("c06w15","metal",[rect("cloth",192,0,256,128),rect("effect",0,0,64,128)])
for _id in ("00","01"):
    weapon("c10w"+_id,"wood_wall",[rect("cloth",64,0,128,256),rect("metal",0,96,64,128)])
weapon("c10w02","metal",[rect("wood_wall",160,0,256,256),rect("cloth",0,0,64,128)])
for _id in ("03","04","05","06","07","08","09"):
    weapon("c10w"+_id,"metal",[rect("leather",128,0,192,256)])
weapon("c10w10","wood_wall",[rect("metal",128,0,192,128)])
weapon("c10w12","metal",[rect("cloth",128,0,192,256)])
weapon("c10w13","grass",[rect("cloth",128,32,192,256),rect("metal",128,0,192,32)])
for _id in ("00","01"):
    weapon("c18w"+_id,"metal",[rect("wood_wall",0,192,256,256),rect("leather",64,128,128,192)])
weapon("c18w02","metal",[rect("wood_wall",128,64,224,208)])
for _id in ("03","04","05","06","07","08","09","10","11","12"):
    weapon("c18w"+_id)
WEAPONS["c18w06"] = ("metal",[rect("wood_wall",0,192,128,256)])
WEAPONS["c18w08"] = ("metal",[rect("effect",0,160,128,256)])
WEAPONS["c18w10"] = ("metal",[rect("effect",128,128,256,256)])
WEAPONS["c18w11"] = ("skin",[rect("metal",128,0,256,96)])
WEAPONS["c18w12"] = ("metal",[rect("leather",128,0,256,256),rect("wood_wall",0,0,64,128)])
ATLASES.update(WEAPONS)
ATLASES.update({
    "d06carpet_b02": ("cloth",[rect("stone",192,0,256,256)]),
    "d06carpet_b06": ("stone",[rect("cloth",128,0,256,128)]),
    "d06boss_b01": ("stone",[]),
    "d06boss_b02": ("stone",[]),
    "d06boss_b04": ("stone",[rect("effect",0,128,256,256),rect("effect",128,0,256,128)]),
    "d06boss_b05": ("stone",[rect("effect",0,128,256,256)]),
    "d06boss_b06": ("stone",[rect("effect",0,128,256,256)]),
    "gallery_chest": ("wood_wall",[rect("effect",0,0,256,64),rect("metal",0,64,256,128),
        rect("metal",0,128,16,256),rect("metal",112,128,144,256),rect("metal",240,128,256,256)]),
})

# Inspected content variants; source stems select independent atlas annotations.
COLLISION_HASHES = {'d01e03': {'505414f9c737ee11': ['d06main_a', 'd06main_b', 'd06main_c', 'd06main_d'],
            '8b0b0165aa0dc51d': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd01t05': {'8ba16f52a801f34b': ['d03boss'],
            'e4aecdd52c08bc95': ['d06main_a', 'd06main_b', 'd06main_c', 'd06main_d'],
            'fd2b7c514a1d16a2': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b01': {'8642a0965966131a': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c'],
            '87882a9739850763': ['d03boss']},
 'd03b02': {'167dcf9d191fe58c': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c'],
            '86d2985ce37615f6': ['d03boss']},
 'd03b03': {'3e7e0ab7c5292744': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c'],
            'c75e84f615421063': ['d03boss']},
 'd03b04': {'433c47aff0320b63': ['d03boss'],
            'ee1be3c9b5ab911f': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b05': {'081520c441face06': ['d03boss'],
            '16895bcb66000401': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b06': {'277e3b72530f9915': ['d03boss'],
            '5bcd9e2cf25fa579': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b07': {'8ec25ed9eb04362e': ['d03boss'],
            'bb52e83ce80b09d9': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b08': {'94f3b0d4310e2d7f': ['d03boss'],
            'b200abae2cbc70be': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b09': {'76e23b1240f5acb9': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c'],
            'bb6b6b9801577c1d': ['d03boss']},
 'd03b10': {'2b701d71e7144f81': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c'],
            'ef65df927f3dd2a1': ['d03boss']},
 'd03b11': {'7baa4c1c08e33379': ['d03boss'],
            'f1da673224dd094b': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b12': {'1de23ced9e4536d4': ['d03boss'],
            '86f336035f3afe53': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b13': {'24331c44e46cb46e': ['d03boss'],
            '2c211d7704e79e5a': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c']},
 'd03b14': {'6f7a3689c2e68ece': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c'],
            'd67128ad84a77cd9': ['d03boss']},
 'd06b01': {'0944294c4763a0e8': ['d06boss'],
            '1b1dd1eeac3de214': ['d06main_d'],
            '414e03195b2a8dd0': ['d06main_c'],
            '69d4b010c66f77f4': ['d06main_a'],
            '6a2640edb49717de': ['d06main_b']},
 'd06b02': {'075ff726ab0963e3': ['d06main_c'],
            '0b968b79871b5430': ['d06main_d'],
            '1d1a758d53784ed7': ['d06main_a'],
            '5b4f4b3e45573e93': ['d06boss'],
            'ef1dba622c635ceb': ['d06main_b']},
 'd06b03': {'34777413620ad7a9': ['d06main_b'],
            '560429567ec69112': ['d06main_d'],
            '677056ea62dda553': ['d06main_c'],
            'bc12e5363cc9d3d8': ['d06boss'],
            'dd8478c206eabf65': ['d06main_a']},
 'd06b04': {'1103441de6f7cc30': ['d06main_d'],
            '1e62e20420f055f3': ['d06main_a'],
            '6f2cf9cb9b4dd184': ['d06boss'],
            '8e21d2205c644673': ['d06main_b'],
            'ec8deeeb33a8ddf6': ['d06main_c']},
 'd06b05': {'2b244fe814a6108f': ['d06main_b'],
            '30db5d90df3bdf86': ['d06main_d'],
            '4bc60d45efcdaccd': ['d06boss'],
            '4f1ddf022c374071': ['d06main_a'],
            '7887073e3de4a562': ['d06main_c']},
 'd06b06': {'168d627a4a6d19a7': ['d06main_c'],
            '6530144a8e7af28c': ['d06boss'],
            '723955a696274c10': ['d06main_d'],
            '79eb8515c8adf01b': ['d06main_a'],
            '7ccde8db20dd8419': ['d06main_b']},
 'd06b07': {'2f49dc4085427b8b': ['d06main_a'],
            '7a62a5754d64ec86': ['d06main_c'],
            'b67b1e8d5f3af884': ['d06main_d'],
            'f59a7ff99c12398d': ['d06main_b'],
            'fb3efb7a8679411d': ['d06boss']},
 'd06b08': {'09f51bdd581e439a': ['d06main_b'],
            '10afd2bb83fbb59b': ['d06main_d'],
            '1dac5e3343ad224d': ['d06main_c'],
            'b5dc1e15a3bd5440': ['d06main_a'],
            'cb0e842841be2335': ['d06boss']},
 'd06b09': {'166dd2bef4ea38be': ['d06main_c'],
            '3c0bc67dc39fd398': ['d06main_b'],
            '930c0d0b86bf0f35': ['d06main_d'],
            'c51ec6624cd3e9a8': ['d06main_a'],
            'cab99b3d265cbef8': ['d06boss']},
 'd06b10': {'2d55ea46f2922a2f': ['d06main_b'],
            'a3954779810de995': ['d06main_c', 'd06main_d'],
            'dfb1fdfba5240c20': ['d06boss'],
            'e21b74393d28ac8c': ['d06main_a']},
 'd06b11': {'41bc0eb1b8e6eaf2': ['d06main_c'],
            '56e8977133669012': ['d06main_d'],
            'c2897b167412d9fd': ['d06main_b'],
            'ef8445df25305e13': ['d06main_a']},
 'd06b12': {'40f05752b3dbce25': ['d06main_d'],
            '7525caf9904451ad': ['d06main_c'],
            '7a8b5a77b48ace4d': ['d06main_a'],
            'a95870b3ea6542f6': ['d06main_b']},
 'd06b13': {'616cdb3e2e8af7ba': ['d06main_c'],
            'cf62440715aba3df': ['d06main_d'],
            'e1d2050f76af2414': ['d06main_b'],
            'ee226bcd5245fa64': ['d06main_a']},
 'd06b14': {'011b91abbb4ae2a8': ['d06main_c'],
            '079665a944c239be': ['d06main_a'],
            '256deaa20d7c8fa1': ['d06main_b'],
            'e5a6ff2252285ad1': ['d06main_d']},
 'd06b15': {'230bac35ad3d52d8': ['d06main_c'],
            '45b969ce1502f15d': ['d06main_b'],
            '613ad44b58d240fb': ['d06main_d'],
            '8bf0606fa32ab5d9': ['d06main_a']},
 'e117a01': {'3927bf30f1afdb55': ['e124a'], 'b34d2dbff5dce280': ['e124a']},
 'e117a02': {'896ec8f10feaff7d': ['e124a'], 'b759fde907f5e1b0': ['e124a']},
 'e34a01': {'8d76736a0b000bf9': ['e82a'], 'b1cdfd59d25581c1': ['e80a']},
 'e34a02': {'205293bd6e223777': ['e82a'], '644c05dae30969b4': ['e80a']},
 'e35a01': {'45d916ef8ec7f397': ['e83a'], 'e526d79d743b8012': ['e81a']},
 'lightling': {'149a37291f2ea5da': ['d03inter', 'd03main_a', 'd03main_b', 'd03main_c'],
               '9b8baa372294196e': ['d06main_a', 'd06main_b', 'd06main_c', 'd06main_d']},
 'minimap1': {'ee0eb79ef6f4b9eb': ['d06main_a', 'd06main_b', 'd06main_c', 'd06main_d'],
              'fe2ab0633820035c': ['d03main_a', 'd03main_b', 'd03main_c']},
 'smajin_beem': {'1d49de76683bb755': ['c23_beem'], '7b91bf1ca6b04f96': ['c17_beem']},
 'smajin_beem_s': {'58d34c874490b36e': ['c17_beem_s'], '89812bd3c3f6f75d': ['c23_beem_s']}}

def initial_records():
    records = {}
    def add(name, profile, width=256, height=256, reason="Authored procedural physical surface detail"):
        records[name] = {"profile":profile, "width":width, "height":height,
            "classification":"excluded" if profile in ("flat","effect") else "authored", "reason":reason}
    for i in range(1,16):
        add(f"d03b{i:02}", "wood_floor" if i in (1,4) else "wood_wall")
    add("d03b15","effect",reason="Boss-room water/portal atlas; no physical surface response")
    add("d03b07","metal",reason="Corroded and painted hatch fittings, worn coins, and excluded liquid/effect islands")
    for i in range(1,16):
        add(f"d06b{i:02}","stone")
    add("d06b02","cloth",reason="Worn carpet fragments over stone; independent cloth/stone atlas annotations")
    add("d06b11","flat",reason="Stained glass; preserve the painted pane without physical bump detail")
    add("d06v01","cloth",128,128)
    for name in ("d03b09","d03b09a","d03b13"):
        add(name,"effect",128,128,"Water/glow effect; neutral physical surface response")
    add("d03b12","metal",128,128)
    add("d03v01","skin")
    add("d03v02","skin",128,128)
    for name in ("c01d01","c01d01dan","c01d11"):
        add(name,"skin",128,128 if name != "c01d11" else 256)
    for name in ("c01d02","c01d03","c01d04","c01d05"):
        add(name,"leather" if name == "c01d03" else "cloth")
    for name in ("c01w00","c01w01"):
        add(name,"metal",128,256)
    add("c01_fuusya","effect",128,128,"Weapon trail; neutral normal and zero wet coverage")
    for name in ("d01t05","e23a01","e23a02","e28a01","e28a02","e52a01","e52a02","e52a03","e61a01","e67a01","e67a02","gatekey03","gatekey06"):
        add(name,ATLASES[name][0])
    add("d00i01","stone",128,128,"Stone pickups; physical detail shared across four authored islands")
    for name in ("alpha01","awabres","basefx00","basefx01","bomb_ex","ceffe_s","d00e01","d00e03","d00e03_a",
                 "d01e02","d01e03","d01e04","d01e05","d04e01","element","fx_foot","fx_foot2","iteme01","iteme02",
                 "lightling","maseki_ex1","maseki_ex2","maseki_ex3","maseki_ex4","nebaneba"):
        add(name,"effect",reason="Particle, flame, shadow or additive effect; explicit neutral surface detail")
    for name in ("ankfnt24","dunenter","floor02","floor05","frame","fuki256","fuki256b","gaiji","itemicon","itempack",
                 "kaisou","minimap1","pause","stayframe","syst04","wepicon","wepready"):
        add(name,"flat",reason="Interface or font atlas; excluded from physical scene materials")
    for name,dimensions in DIMENSIONS.items():
        if name in records:
            records[name]["width"],records[name]["height"] = dimensions
    for profile,names in EXTRA_ENEMIES.items():
        for name in names:
            add(name,profile,128 if profile in ("effect","flat") or name in ENEMY_128 else 256,
                128 if profile in ("effect","flat") or name in ENEMY_128 else 256,
                "Audited reachable enemy atlas" if profile not in ("effect","flat") else "Explicit enemy spell or card effect exclusion")
    for name,(profile,_) in ATLASES.items():
        if name.startswith(("c13","c17","c23","e")) and name not in records:
            dimension = 128 if name in ENEMY_128 else 256
            add(name,profile,dimension,dimension,"Audited reachable enemy atlas with independently authored material regions")
    records["c17a01"]["width"] = records["c17a01"]["height"] = 512
    records["c17a02_an"]["width"],records["c17a02_an"]["height"] = 128,192
    for name in ("c17a03_an","c17a04_an"):
        records[name]["width"],records[name]["height"] = 384,128
    add("e26a02","skin",128,128,"Organic creature atlas")
    for name,(profile,_) in PARTY.items():
        size = 128 if name in ("c04b01","c05a01","c06a01","c10a01") else 256
        add(name,profile,size,size,"Audited playable character atlas with physical material islands")
    for name,(profile,_) in WEAPONS.items():
        add(name,profile,128,128,"Audited equippable weapon atlas with physical material islands")
    add("c05w_h","effect",512,256,"Magic weapon particle atlas")
    add("c05_f01","effect",128,128,"Playable mage fire particle atlas")
    add("mgan01","effect",128,128,"Gun muzzle-flash sprite")
    for name in ("d01b20_a","d01b20_b","d01b20_c","s140101","s140102","s140105","s140106","s400107","s400108","s400109"):
        add(name,"effect",reason="Animated water, sky or portal/lightning effect; neutral physical detail")
    for name in ("d01b20_a","d01b20_b","d01b20_c","s400108","s400109"):
        records[name]["width"] = records[name]["height"] = 128
    records["s140101"]["width"] = records["s140101"]["height"] = 512
    return records


def png(pixels, width, height):
    def chunk(kind, payload):
        data = kind + payload
        return struct.pack(">I",len(payload)) + data + struct.pack(">I",zlib.crc32(data))
    rows = b"".join(b"\0" + pixels[y*width*4:(y+1)*width*4] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR",struct.pack(">2I5B",width,height,8,6,0,0,0))
        + chunk(b"IDAT",zlib.compress(rows,9)) + chunk(b"IEND",b""))


def height(u,v,frequency,kind):
    tau = 2*math.pi
    if kind == "grain":
        warp = .43*math.sin(tau*(3*u+v)) + .21*math.sin(tau*(7*u-2*v))
        return (.50*math.sin(tau*(frequency*v+warp+.18*math.sin(tau*11*v)))
            + .25*math.sin(tau*((frequency+36)*v+.31*math.sin(tau*5*u)))
            + .15*math.sin(tau*((frequency+72)*v+.23*math.sin(tau*(7*u+3*v)))))
    if kind == "weave":
        return .5*math.sin(tau*frequency*u) + .5*math.sin(tau*frequency*v)
    if kind == "brushed":
        return .7*math.sin(tau*frequency*v) + .2*math.sin(tau*3*frequency*v)
    if kind == "stone":
        return .48*math.sin(tau*(frequency*u+3*v)) + .43*math.cos(tau*(frequency*v-2*u)) + .19*math.sin(tau*(29*u+17*v))
    if kind == "pebble":
        return .45*math.sin(tau*frequency*u)*math.sin(tau*frequency*v) + .25*math.cos(tau*(frequency//2*u+frequency//3*v))
    if kind == "grass":
        return .6*math.sin(tau*(frequency*u+4*v))*math.sin(tau*7*v)
    if kind == "corrosion":
        return .70*noise(u,v,frequency)+.25*noise(u,v,53)+.05*noise(u,v,101)
    return 0.


def noise(u,v,period):
    x,y = (u%1)*period,(v%1)*period
    ix,iy = math.floor(x),math.floor(y)
    fx,fy = x-ix,y-iy
    fx,fy = fx*fx*(3-2*fx),fy*fy*(3-2*fy)
    def value(dx,dy):
        seed = ((ix+dx)%period)*374761393+((iy+dy)%period)*668265263
        seed = ((seed^(seed>>13))*1274126177)&0xffffffff
        return ((seed^(seed>>16))&0xffff)/32767.5-1
    a,b,c,d = value(0,0),value(1,0),value(0,1),value(1,1)
    return (a+(b-a)*fx)*(1-fy)+(c+(d-c)*fx)*fy


def byte(value):
    return max(0,min(255,round(value*255)))


def inside(x,y,region):
    if "rect" in region:
        x0,y0,x1,y1 = region["rect"]
        return x0 <= x < x1 and y0 <= y < y1
    hit = False
    previous = region["polygon"][-1]
    for current in region["polygon"]:
        x0,y0 = previous
        x1,y1 = current
        if (y0 > y) != (y1 > y) and x < (x1-x0)*(y-y0)/(y1-y0)+x0:
            hit = not hit
        previous = current
    return hit


def material_at(name,profile,u,v):
    x,y = u*256,v*256
    if name in ATLASES:
        profile,regions = ATLASES[name]
        for region in regions:
            if inside(x,y,region):
                profile = region["material"]
    for cx,cy,rx,ry in MOSS_ISLANDS.get(name,[]):
        wobble = .12*math.sin(x*.83+y*.49) + .12*math.sin(x*.31-y*.71)
        if ((x-cx)/rx)**2 + ((y-cy)/ry)**2 < .70+wobble:
            profile = "moss"
    return profile


def finish_at(name,u,v):
    if name not in ATLAS_FINISHES:
        return None
    finish = ATLAS_FINISHES.get(name)
    if name in ATLASES:
        for region in ATLASES[name][1]:
            if inside(u*256,v*256,region):
                finish = region.get("finish",ATLAS_FINISHES.get(name))
    return finish


def raster(name,profile,width,height_px):
    normal,orm,mask = bytearray(),bytearray(),bytearray()
    for y in range(height_px):
        for x in range(width):
            u,v = (x+.5)/width,(y+.5)/height_px
            surface = material_at(name,profile,u,v)
            finish = finish_at(name,u,v) if surface not in ("flat","effect") else None
            roughness,metalness,amplitude,frequency,kind = FINISHES[finish] if finish else PROFILES[surface]
            # Deck planks and upright wall boards use vertical authored grain.
            rotated = kind == "grain" and name in ("wood_floor","d03b01","d03b03","d03b04","d03b11")
            hu,hv = (v,u) if rotated else (u,v)
            h = height(hu,hv,frequency,kind)
            dx = (height(hu+1/SIZE,hv,frequency,kind)-height(hu-1/SIZE,hv,frequency,kind))*amplitude
            dy = (height(hu,hv+1/SIZE,frequency,kind)-height(hu,hv-1/SIZE,frequency,kind))*amplitude
            if rotated:
                dx,dy = dy,dx
            length = math.sqrt(dx*dx+dy*dy+1)
            physical = surface not in ("flat","effect")
            normal.extend((byte(.5-dx/(2*length)),byte(.5-dy/(2*length)),byte(.5+1/(2*length)),255 if physical else 0))
            wet = 0.
            if surface == "wood_floor":
                ridge = max(0.,h-.12)**2
                patch = .48+.28*math.sin(2*math.pi*(5*u+2*v))+.24*math.cos(2*math.pi*(3*v-2*u))
                patch += .16*math.sin(2*math.pi*(13*v+u))
                wet = min(1.,ridge*max(0.,patch)*4.0)
                roughness = .85-.33*wet+.025*h
            elif surface == "stone" and (name in WET_ATLASES or any(inside(u*256,v*256,region) for region in WET_REGIONS.get(name,[]))):
                patch = math.sin(2*math.pi*(3*u+.27*math.sin(2*math.pi*2*v))) + .55*math.cos(2*math.pi*(2*v-u))
                wet = min(1.,max(0.,(patch-.12)*1.5))
                roughness = .90-.48*wet+.02*h
            else:
                roughness += .025*h if physical else 0.
            orm.extend((255,byte(roughness),byte(metalness),byte(wet)))
            mask.extend((IDS[surface],255,0,255))
    return bytes(normal),bytes(orm),bytes(mask)


def build():
    files,profiles = {},{}
    for name in PROFILES:
        normal,orm,_ = raster(name,name,SIZE,SIZE)
        files[f"{name}_normal.png"] = png(normal,SIZE,SIZE)
        files[f"{name}_orm.png"] = png(orm,SIZE,SIZE)
        profiles[name] = {"normal":f"{name}_normal.png","orm":f"{name}_orm.png","semantic_id":IDS[name],
            "note":"Original procedural detail; authored retail UVs retained"}
    def emit_record(name,record):
        profile = record["profile"]
        if name in ATLASES:
            normal,orm,mask = raster(name,profile,record["width"],record["height"])
            for suffix,pixels in (("normal",normal),("orm",orm),("regions",mask)):
                filename = f"{name}_{suffix}.png"
                files[filename] = png(pixels,record["width"],record["height"])
                record["region_mask" if suffix == "regions" else suffix] = filename
            record["regions"] = [{"material":ATLASES[name][0],"rect":[0,0,256,256]}]+ATLASES[name][1]
            if name in ATLAS_FINISHES:
                record["base_finish"] = ATLAS_FINISHES[name]
                record["regions"][0]["finish"] = ATLAS_FINISHES[name]
            if name in MOSS_ISLANDS:
                record["moss_islands"] = MOSS_ISLANDS[name]
            physical = ATLASES[name][0] not in ("flat","effect") or any(region["material"] not in ("flat","effect") for region in ATLASES[name][1])
            record["classification"] = "authored" if physical else "excluded"
        else:
            record.update(normal=profiles[profile]["normal"],orm=profiles[profile]["orm"])
        record["wet_eligible"] = name in WET_ATLASES or name in WET_REGIONS or profile == "wood_floor"
        if name in WET_REGIONS:
            record["wet_regions"] = WET_REGIONS[name]
    records = initial_records()
    for name,record in sorted(records.items()):
        emit_record(name,record)
    for name,(main_hash,boss_hash) in VARIANT_HASHES.items():
        main = dict(records[name])
        profile = "effect" if name == "d03b14" else "stone"
        boss = {"profile":profile,"width":256,"height":256,
            "classification":"excluded" if profile == "effect" else "authored",
            "reason":"Shipwreck boss-room atlas variant; independent physical UV annotations"}
        emit_record("d03boss_"+name[3:],boss)
        records[name]["variants"] = {main_hash:main,boss_hash:boss}
    for name,variants in COLLISION_HASHES.items():
        if name not in records or name in VARIANT_HASHES:
            continue
        base = {key:value for key,value in records[name].items() if key != "variants"}
        records[name]["variants"] = {}
        for fingerprint,sources in variants.items():
            variant = dict(base)
            atlas_name = name
            if name.startswith("d06b") and "d06boss" in sources:
                profile = "stone" if name in ("d06b01","d06b02","d06b04","d06b05","d06b06") else "effect"
                variant = {"profile":profile,"width":256,"height":256,
                    "classification":"authored" if profile == "stone" else "excluded",
                    "reason":"Gallery boss-room variant with independent stone/effect regions"}
                if name in ("d06b08","d06b09","d06b10"):
                    variant["width"] = variant["height"] = 128
                atlas_name = "d06boss_"+name[3:]
            elif name in ("d06b02","d06b06") and any(s in sources for s in ("d06main_b","d06main_c","d06main_d")):
                atlas_name = "d06carpet_"+name[3:]
                variant["profile"] = "cloth" if name == "d06b02" else "stone"
                variant["reason"] = "Later-floor carpet variant; physical cloth islands replace worn fragments"
            elif name == "d01t05" and any(s.startswith("d06") for s in sources):
                atlas_name = "gallery_chest"
                variant["reason"] = "Gallery treasure atlas; luminous top islands excluded from physical wood and metal"
            if atlas_name != name:
                emit_record(atlas_name,variant)
            records[name]["variants"][fingerprint] = variant
    manifest = {"version":2,"orm_channels":{"r":"ambient_occlusion","g":"perceptual_roughness","b":"metalness","a":"wet_coverage"},
        "normal_alpha":"zero excludes physical specular response and bump detail; positive values encode physical detail strength",
        "region_mask_channels":{"r":"semantic_id","g":"authored","b":"reserved","a":"opaque"},
        "region_coordinate_extent":256,"profiles":profiles,"texture_records":dict(sorted(records.items()))}
    manifest["variant_fingerprint"] = "fnv1a64(le_u32(width) || le_u32(height) || rgba8_row_major)"
    manifest["finishes"] = {name:{"roughness":values[0],"metalness":values[1],"detail":values[4]} for name,values in FINISHES.items()}
    files["manifest.json"] = (json.dumps(manifest,indent=2)+"\n").encode()
    return files


def validate(files):
    manifest = json.loads(files["manifest.json"])
    images,checked_rasters = {},{}
    for name,data in files.items():
        if not name.endswith(".png"):
            continue
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"Invalid PNG signature: {name}")
        width,height,depth,kind = struct.unpack_from(">IIBB",data,16)
        if depth != 8 or kind != 6:
            raise ValueError(f"Expected RGBA8 PNG: {name}")
        cursor,payload = 8,bytearray()
        while cursor < len(data):
            length = struct.unpack_from(">I",data,cursor)[0]
            tag = data[cursor+4:cursor+8]
            body = data[cursor+8:cursor+8+length]
            crc = struct.unpack_from(">I",data,cursor+8+length)[0]
            if zlib.crc32(tag+body) != crc:
                raise ValueError(f"PNG CRC mismatch: {name}")
            if tag == b"IDAT":
                payload.extend(body)
            cursor += length+12
        raw = zlib.decompress(payload)
        if len(raw) != height*(width*4+1) or any(raw[y*(width*4+1)] != 0 for y in range(height)):
            raise ValueError(f"PNG raster mismatch: {name}")
        images[name] = width,height
        if name in ("d03b07_normal.png","d03b07_orm.png","d03b07_regions.png"):
            checked_rasters[name] = b"".join(raw[y*(width*4+1)+1:(y+1)*(width*4+1)] for y in range(height))
    references = set()
    def record(entry):
        if entry.get("profile") not in PROFILES:
            raise ValueError("Unknown semantic profile")
        for key in ("normal","orm","region_mask"):
            if key not in entry:
                continue
            leaf = entry[key]
            if pathlib.PurePath(leaf).name != leaf or leaf not in images:
                raise ValueError(f"Unpackaged material reference: {leaf}")
            references.add(leaf)
        if images[entry["normal"]] != images[entry["orm"]]:
            raise ValueError("Normal/ORM dimension mismatch")
        if "region_mask" in entry and images[entry["region_mask"]] != (entry["width"],entry["height"]):
            raise ValueError("Atlas mask/source dimension mismatch")
        for fingerprint,variant in entry.get("variants",{}).items():
            if len(fingerprint) != 16 or any(c not in "0123456789abcdef" for c in fingerprint):
                raise ValueError("Invalid variant fingerprint")
            record(variant)
    for profile,entry in manifest["profiles"].items():
        record({"profile":profile,**entry})
    for entry in manifest["texture_records"].values():
        record(entry)
    if references != set(images):
        raise ValueError(f"Unreferenced PNGs: {sorted(set(images)-references)}")
    main_id,boss_id = VARIANT_HASHES["d03b07"]
    plate = manifest["texture_records"]["d03b07"]
    if plate["profile"] != "metal" or plate["variants"][main_id]["profile"] != "metal" or plate["variants"][boss_id]["profile"] != "stone":
        raise ValueError("Shipwreck fitting and boss-room stone variants require distinct semantic profiles")
    def sample(suffix,u,v):
        data = checked_rasters["d03b07_"+suffix+".png"]
        pixel = (int(v*256)*256+int(u*256))*4
        return data[pixel:pixel+4]
    # Both upward floor and downward ceiling triangles use the corroded hatch
    # island. Its finish must not become brushed bare metal or wet timber.
    for u,v in ((.142952,.584398),(.248,.748),(.102,.504),(.004,.602),(.625,.875)):
        orm = sample("orm",u,v)
        if sample("regions",u,v)[0] != IDS["metal"] or orm[1] < 215 or orm[2] > 31 or orm[3] != 0 or sample("normal",u,v)[3] != 255:
            raise ValueError("Corroded hatch UV must retain rough dielectric physical response")
    coin = sample("orm",.75,.25)
    if coin[2] < 204 or not 150 <= coin[1] <= 175:
        raise ValueError("Coin island must retain worn conductive metal")
    if sample("normal",.9,.75)[3] != 0 or sample("regions",.9,.75)[0] != IDS["effect"]:
        raise ValueError("Liquid/effect island must exclude physical response")
    hatch_red = [sample("normal",x/256,y/256)[0] for y in range(136,248,4) for x in range(8,120,4)]
    if min(hatch_red) >= 127 or max(hatch_red) <= 128:
        raise ValueError("Corrosion detail must vary across both axes, not form brushed stripes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check",action="store_true",help="Verify deterministic output without modifying assets")
    args = parser.parse_args()
    files = build()
    validate(files)
    if args.check:
        errors = [n for n,d in files.items() if not (OUT/n).is_file() or (OUT/n).read_bytes() != d]
        extras = sorted(p.name for p in OUT.glob("*.png") if p.name not in files)
        if errors or extras:
            raise SystemExit(f"Material output mismatch: {errors}; stale PNG files: {extras}")
    else:
        OUT.mkdir(parents=True,exist_ok=True)
        for name,data in files.items():
            (OUT/name).write_bytes(data)
        for file in OUT.glob("*.png"):
            if file.name not in files:
                file.unlink()
    digest = hashlib.sha256(b"".join(n.encode()+d for n,d in sorted(files.items()))).hexdigest()
    print(f"Verified {len(files)-1} RGBA maps, {len(initial_records())} exact records; sha256 {digest}")


if __name__ == "__main__":
    main()
