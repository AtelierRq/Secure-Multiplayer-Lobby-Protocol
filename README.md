# Secure-Multiplayer-Lobby-Protocol


Komenda do uruchomienia pliku generowania certyfikatów:

powershell -ExecutionPolicy Bypass -File .\generate_certs.ps1


Kompilacja:

mingw32-make


---------------------------------------------------------------------------------------------------
Terminal 1:

Uruchomienie serwera: bin\smlp_server.exe 1234 
---------------------------------------------------------------------------------------------------

---------------------------------------------------------------------------------------------------
Terminal 2:

Uruchomienie klienta: bin\smlp_client.exe 127.0.0.1 1234

Klient podaje niezajęty nick.

Klient tworzy lobby: CREATE_LOBBY|Room1

Klient dołącza do lobby: JOIN|Room1

Klient opuszcza lobby: LEAVE

WYświetlanie listy pokoi: LIST_LOBBIES

Wyświetlanie listy graczy: LIST_PlAYERS

---------------------------------------------------------------------------------------------------