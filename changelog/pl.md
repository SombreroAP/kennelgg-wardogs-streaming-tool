# Kennel.gg Wardogs Streaming Tool — Lista zmian

## 0.28.2
- **Każdy szybki wybór pokazuje $ na minutę.** Kasa zarobiona na minutę w grze to najuczciwsza miara tego, ile robisz dla swojej strony, niezależnie od roli, więc paski Fragger, Medyk, Zwiadowca i Kierowca też ją teraz pokazują (Logistyk, Budowniczy, Cele i Uniwersalny już ją miały).

## 0.28.1
- **Najdłuższego zabójstwa nie ma już na pasku statystyk sesji.** Zniknęło z opcji paska i z szybkiego wyboru „Fragger”, który zamiast niego pokazuje kasę z walki. Pasek, na którym było wybrane, po prostu je pomija. Nadal jest na obrazku ze statystykami i w podsumowaniu po zakończeniu streama.

## 0.28.0
- **Statystyki dla każdego stylu gry, nie tylko K/D/A.** Pasek statystyk sesji może teraz pokazywać, co robią medycy, zwiadowcy, logistycy, budowniczowie i kierowcy: leczenia, wykrytych wrogów, dostarczone zaopatrzenie, zbudowane rzeczy i przewiezionych pasażerów, a także kasę, którą przyniosła każda z tych ról (Medyk $, Zwiad $, Logistyka $, Budowa $, Transport $, Cele $, Walka $). Ustawienia, Klipy & powtórki: zaznacz je w "Pasek na streamie pokazuje" albo użyj opcji **Szybki wybór**, żeby dostać gotowy pasek: Fragger, Medyk, Zwiadowca, Logistyk, Budowniczy, Kierowca, Cele albo Uniwersalny. Powód obok twojego stanu konta pokazuje też LECZENIE, SPOT, DOSTAWA, BUDOWA albo TRANSPORT, a obrazek ze statystykami zamienia dolny rząd na twoje liczby wsparcia, jeśli je masz.
- **Naprawione: podniesienia nie były liczone.** Gra pisze REVIVE, TEAMMATE REVIVED i HOT ZONE REVIVE; wtyczka znała tylko inne sformułowanie, więc liczba podniesień mogła stać na 0. Teraz używa sformułowań z prawdziwych meczów, tak samo jak pasażerowie (PASSENGER TRANSPORT), dostawy (SUPPLIES DELIVERED) i zabójstwa z dużej odległości.
- Leczenia, spoty i budowanie są rozpoznawane po słowach, bo ich dokładnego brzmienia nie widziano jeszcze w meczu. Nagroda, której wtyczka nie umie przypisać, trafia raz do logu ("reward line not recognised"); wyślij ją w zgłoszeniu, a dostanie swoją rolę. Są czytane z angielskich linii nagród, tak jak asysty i podniesienia.
- **Najdłuższe zabójstwo** było już jedną z opcji paska i należy do wyboru Fragger.

## 0.27.0
- **Wtyczka mówi w twoim języku.** Ustawienia, dok, konfiguracja, panel Drużyna i wszystkie komunikaty są teraz dostępne w 14 językach: English, Deutsch, Français, Español, Italiano, Português (Brasil), Polski, Türkçe, Русский, Українська, 日本語, 한국어, 简体中文 i 繁體中文, czyli w tych samych 14 co gra. Wtyczka używa języka OBS, chyba że wybierzesz inny: Ustawienia, Ogólne, **Język wtyczki**. Zmiana działa od razu, bez restartu.
- **Na streamie też.** Stingery SZYBKA POWTÓRKA i ZMIANA POV, ramka NA ŻYWO, tag powtórki, tag z nazwą, pasek statystyk sesji i jego powody (ZABÓJSTWO, PODNIESIENIE, ZAKUP...), obrazek ze statystykami i plansze tytułowe najlepszych momentów są w wybranym języku. Japoński, koreański, chiński i cyrylica są rysowane pasującymi czcionkami Windows, a długi tytuł zmniejsza się, żeby zmieścić się na ekranie. Etykieta powtórki albo tag z nazwą, które wpisałeś sam, zostają tak, jak je napisałeś.
- Pozycja w menu Narzędzia w OBS i nazwy skrótów klawiszowych wtyczki są w tym samym języku co OBS.
- **Informacje o wersji też w twoim języku.** Powiadomienie o aktualizacji w pluginie pokazuje nowości w wybranym języku, a każdy język ma własną stronę z listą zmian na kennel.gg/streaming/changelog.
- Komendy głosowe to nadal angielskie słowa ("hey kennel replay"), a plik logu wtyczki zostaje po angielsku, żeby dało się go przeczytać, gdy poprosisz o pomoc.

## 0.26.6
- **Szybka powtórka ma menu, tak jak Zapisz klip.** Strzałka obok Szybkiej powtórki oferuje **Zapisz klip i odtwórz powtórkę**: zapisuje klip w tej samej chwili i odtwarza go jako powtórkę, gdy tylko się zapisze. **Powtórz ostatni klip** robi to samo co przycisk.
- **Licznik zabójstw pod celownikiem też jest czytany.** Gdy kogoś zabijesz, gra pokazuje pod celownikiem sumę w ramce ("+$1,750") na własnym ciemnym tle, więc da się ją odczytać tam, gdzie linie pod twoim stanem konta giną na tle białego nieba. ClipHound czyta teraz tę sumę pięć razy na sekundę, gdy statystyki sesji są włączone. Gdy portfel pokazuje kasę, której nie uwzględniły linie w rogu, część pokazana przez licznik zabójstw liczy się jako kasa za ZABÓJSTWO, a tylko reszta jako NAGRODA. Leczenie, spotowanie i strefy pojawiają się tylko w rogu, więc te dalej są brane stamtąd. O sumie nadal decyduje portfel, więc wszystko zawsze zgadza się z tym, co pokazuje twój stan konta. Sprawdzone na prawdziwym nagraniu w 1440p: +$1,750, +$1,500, +$3,000, +$2,750 i +$5,500 z quad killa wyszły jako jedna seria z poprawną sumą.

## 0.26.5
- **Bilans sesji zawsze zgadza się z portfelem.** Wcześniej był sumą linii nagród, więc linia, którą czytnik przegapił (biały tekst na białym niebie) albo źle odczytał, zaburzała sumę do końca sesji. Teraz rozstrzyga twój stan konta na HUD-zie: gdy portfel stoi w miejscu przez 4 sekundy, bilans sesji jest wyrównywany dokładnie do tego, o ile portfel zmienił się od początku sesji. Wszystko, czego nie uwzględniły linie, jest dodawane jako NAGRODA (albo WYDANE, gdy zmiana była w drugą stronę), a kasa, która zmieniła się, gdy HUD był ukryty, np. wypłata między meczami, jest liczona przy następnym pojawieniu się stanu konta na ekranie. Log zapisuje każdą korektę.

## 0.26.4
- **Film o konfiguracji.** Sombrero instaluje wtyczkę i konfiguruje ją od początku do końca na YouTube. Jest pod ręką: na górze Ustawienia, Pomoc, w menu ⋯ doku (Film o konfiguracji) i na pierwszej stronie konfiguracji.
- **Pojazdy: podwójne okno otwiera się tylko dla kolegi z drużyny, który jest na żywo.** Z opcją "Auto w pojazdach" wejście do pojazdu otwiera podwójne okno tylko wtedy, gdy twój kolega z drużyny do podwójnego POV jest potwierdzony na żywo - na żywo na twoim kanale głosowym Kennel.gg w przypadku Discorda albo na żywo na Twitchu, Kicku lub YouTube. Jeśli nie jest, okno czeka i otwiera się, gdy tylko będzie, o ile nadal jesteś w pojeździe, a okno, które się otworzyło, zamyka się, jeśli przestanie streamować.
- **Ekwipunek: POV kolegi z drużyny wchodzi na ekran tylko wtedy, gdy jego stream jest potwierdzony na żywo.** Otwarcie ekwipunku (ładowanie magazynków) przełączało wcześniej na kolegę, którego streamu nie dało się sprawdzić, przez co na stream mógł trafić pusty feed. Teraz przełącza tylko na kogoś potwierdzonego na żywo (koledzy z VDO.Ninja i ze źródła OBS, których nie da się sprawdzić, nadal się liczą), a w przeciwnym razie zostaje na twoim POV.

## 0.26.3
- **Dok jest na ekranie od początku.** OBS uruchamia dok nowo zainstalowanej wtyczki jako ukryty, więc trzeba go było szukać w Widok, Doki. Teraz wtyczka raz wstawia go na ekran, zadokowany po prawej stronie okna OBS, przy pierwszym uruchomieniu OBS z tą wersją, także u każdego, kto zainstalował wcześniejszą i nigdy go nie znalazł. Zamknij go, a zostanie zamknięty; Widok, Doki przywraca go z powrotem.
