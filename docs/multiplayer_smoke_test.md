# Multiplayer Smoke Test (Alpha)

1. No editor, abra o menu `Run -> Multiplayer Local Test...`.
2. Defina `Server = 127.0.0.1`, `Port = 27015`, host/client nicknames.
3. Clique `Launch Local Pair`.
4. Verifique no Inspector do `Multiplayer Manager` em cada instância:
   - `Runtime` muda para `Hosting` no host e `Connected` no client.
   - `Connected` fica `yes`.
   - `Players` >= 2.
5. Mova o player local no host e confirme sincronismo no client.
6. Altere um channel float/bool e confirme replicação.
7. Passe por trigger e confirme alteração de channel e resposta no outro lado.
8. Teste áudio com `Gate Channel` e confirme ganho/runtime no outro lado.
9. Em uma instância client, desligue rede (ou feche host) e valide fallback:
   - estado muda para `Connecting`,
   - volta sozinho após host retornar.
10. Abra script Lua e valide:
   - `scene.isConnected()`,
   - `scene.isHost()`,
   - `scene.playerCount()`.
