"""The telemetry of whichever game an agent runs: Indy 500's (work RAM) or
Touring Car's (scratch RAM), the same fields for the drivers."""
import indy500_telemetry
import stcc_telemetry

FPS = 57.5


def module(game):
    return stcc_telemetry if game.startswith("stcc") else indy500_telemetry


def read(agent):
    return module(agent.game).read(agent)
