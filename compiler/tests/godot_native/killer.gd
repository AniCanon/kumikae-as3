extends Node

# Frees the node it was told about, when the probe's script asks, and keeps what the script says afterwards.
var victim: Node
var said := "nothing"


func kill() -> void:
	victim.free()


func report(text: String) -> void:
	said = text
