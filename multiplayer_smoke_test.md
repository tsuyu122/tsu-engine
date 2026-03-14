# Multiplayer Smoke Test

- Objetivo: validar host/client local, spawn/despawn básico e sync de transform/ownership.
- Pré-requisito: cena com Multiplayer Manager ativo e um Multiplayer Controller com IsLocalPlayer.

## Passo a passo (Editor)
- Menu Run → Multiplayer Local Test…
- Server: 127.0.0.1, Port: 27015 (padrão)
- Host Nick: Host, Client Nick: Client
- Clique “Launch Local Pair” (abre duas instâncias de GameRuntime).
- Na janela Host, use WASD/mouse para mover o player local; observe sync no Client.
- Feche uma janela e valide “AutoReconnect” se habilitado no Manager (inspector).

## Passo a passo (Lua)
- isConnected(), isHost(), playerCount() para status.
- setNickname(entity_idx, "MeuNick") em Controller.
- Exemplos:
```
function OnStart()
  print("Connected?", scene.isConnected())
  print("Players:", scene.playerCount())
end
```

## CLI (GameRuntime)
- Argumentos:
  - --mp-mode=host|client
  - --mp-server=127.0.0.1
  - --mp-port=27015
  - --mp-nick=Player

## Troubleshooting
- Porta bloqueada: revise firewall.
- Sem Manager na cena: GameRuntime cria automaticamente um.
- NetworkId zero: setNickname() cria id com base no nickname.
