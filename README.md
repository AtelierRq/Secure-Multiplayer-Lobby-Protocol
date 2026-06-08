# Uruchomienie serwera

1. Przejdź do katalogu głównego projektu.

2. Wygeneruj certyfikaty TLS:

```powershell
powershell -ExecutionPolicy Bypass -File .\generate_certs.ps1
```

3. Upewnij się, że wygenerowane certyfikaty znajdują się w katalogu:

```text
certs/
```

4. Skompiluj projekt:

```bash
mingw32-make
```

5. Uruchom serwer:

```bash
bin\smlp_server.exe 1234
```

gdzie:

* `1234` – numer portu nasłuchiwania.

Po poprawnym uruchomieniu powinien pojawić się komunikat:

```text
[SMLP] Server listening on port 1234
```

---

# Uruchomienie klienta

Uruchom klienta podając adres serwera oraz numer portu:

```bash
bin\smlp_client.exe 127.0.0.1 1234
```

gdzie:

* `127.0.0.1` – adres serwera,
* `1234` – numer portu serwera.

Po nawiązaniu połączenia klient poprosi o podanie nazwy użytkownika (nickname).

---

# Zakończenie działania klienta

Aby zakończyć działanie klienta wpisz:

```text
exit
```

lub zamknij okno terminala.

---

# Zakończenie działania serwera

Zamknięcie okna serwera powoduje zakończenie działania aplikacji oraz rozłączenie wszystkich podłączonych klientów.


---

# Pełna lista komend znajduje się w /docs
