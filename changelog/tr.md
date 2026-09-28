# Kennel.gg Wardogs Streaming Tool — Değişiklik günlüğü

## 0.29.3
- **BETA etiketi.** Panel adının yanında BETA gösteriyor, Kurulum'un ilk sayfası da eklentinin beta aşamasında olduğunu söylüyor: hâlâ geliştiriliyor ve sık değişiyor, ara sıra hata olabilir; bir sorun olduğunda günlüklerin nereye gönderileceğini de anlatıyor.
- **Günlükleri gönder, Ayarlar > Yardım'a taşındı.** Panelin altındaki düğme kalktı; artık Ayarlar > Yardım'da ("Bir sorun mu var? Günlüklerini bize gönder") ve hâlâ panelin ⋯ menüsünde ve Günlükler penceresinde.

## 0.29.2
- **Günlükleri gönder, tek tıkla.** Bir şeyler ters mi gidiyor? Panelin altındaki (ya da ⋯ menüsündeki veya Günlükler penceresindeki) **Günlükleri gönder** düğmesine bas, istersen ne olduğunu yaz; eklentinin günlüğü ve ayarları, bu OBS oturumunun günlüğü ve ClipHound'un günlüğü doğrudan Kennel.gg'ye gider. #obs-streaming-tool-chat kanalında belirtmen için LOG-7F3A gibi bir referans kodu alırsın. Gizli hiçbir şey gönderilmez (hesap bağlantısı, anahtar ya da şifre yok), kliplerin, videon, sesin veya sohbetin asla gönderilmez ve günlükler 60 gün saklanır.
- **Takım arkadaşının görüntüsü her seferinde ekranı kaplıyor.** Görüntü yalnızca ilk eklendiğinde tam ekran yapılıyordu; bu yüzden tuval büyüdükten (1080p'den 1440p'ye) ya da görüntü yanlışlıkla sürüklendikten sonra küçük kalıyor, ekranın yaklaşık dörtte üçünde duruyordu. Artık her gösterildiğinde yeniden tam ekran yapılıyor ve Twitch, Kick, YouTube veya VDO.Ninja görüntüsü tuval boyutunda işleniyor. Ayarlar, Takım & POV: kendi düzenini korumak için "Takım arkadaşının görüntüsü her gösterildiğinde tüm ekranı kaplasın" seçeneğinin işaretini kaldır.

## 0.29.1
- **Düzeltildi: yayın, paylaşımı durdurmuş bir takım arkadaşında takılı kalabiliyordu.** Bir Discord takım arkadaşının paylaşımı, onun POV'u ekrandayken bittiğinde seninkine dönüş reddediliyordu ("Önce bir takım arkadaşı ekle."), bu yüzden yayın onun görüntüsünde kalıyor ve Ben düğmesi sadece bu mesajı tekrarlıyordu. Kendi POV'una dönmek artık her zaman çalışıyor.
- **Düzeltildi: ekipman satıcısında POV'un ileri geri gidip gelmesi.** Şarjör doldurma geçişi satıcı ekranını envanter sanıyor ve bir an sonra bırakıyordu, tekrar tekrar. Satıcı ekranı artık ayırt ediliyor, envanter ipucunun düzgün okunması gerekiyor (yarım kelime değil), okunamayan tek bir kare artık kapanma sayılmıyor ve geri döndükten sonra 15 saniye boyunca yeni bir envanter geçişi olmuyor.
- **Düzeltildi: yanlış okunan bakiye kazanılan para olarak sayılıyordu.** Bir oturum, bakiyenin yanlış okunmasını başlangıç noktası alıp farkı daha sonra tek seferde kazanç olarak ekleyebiliyordu (bir raporda +$36,040). Başlangıç bakiyesinin sayılması için artık birkaç saniye sabit kalması gerekiyor ve bakiyen ekrandayken hiçbir ödülün ya da leş serisinin açıklamadığı $10,000 veya daha büyük bir sıçrama para değil, yanlış okuma sayılıyor.

## 0.29.0
- **Güncelleme tek tık.** Yeni bir sürüm çıktığında, OBS açılırken bir **Yenilikler** penceresi seninkinden bu yana değişen her şeyi kendi dilinde listeler (yayındayken asla; yayındaysan yayın bitene kadar bekler). **Şimdi güncelle** sen devam ederken arka planda indirir ve sürümün sağlama toplamıyla doğrular. Ardından **Yükle ve OBS'i yeniden başlat** OBS'i kapatır, yükler ve OBS'i yeniden açar; tüm ayarların korunur. **Sonra** bir dahaki sefere yine sorar; **Bu sürümü atla** bir sonraki sürüme kadar sormaz.
- Paneldeki güncelleme notu artık kehribar renginde ve neyin yeni olduğunu söylüyor, Şimdi güncelle ve Yenilikler düğmeleriyle; eklenti de yalnızca OBS açılırken değil, 6 saatte bir yeni sürüm olup olmadığına bakıyor.
- Yükleyici, ClipHound'un dosyalarını değiştirmeden önce onu kendisi kapatır; böylece bir güncelleme hiçbir zaman kullanımdaki bir dosyada takılmaz. Taşınabilir sürüm ve Windows olmayan her şey, indirme sayfasını açan İndir düğmesini korur.

## 0.28.2
- **Her hızlı seçim dakika başına $ gösteriyor.** Oyunda dakika başına kazanılan para, rolün ne olursa olsun tarafın için ne kadar iş yaptığının en adil ölçüsü; bu yüzden Fragger, Medik, Keşifçi ve Sürücü çubukları da artık bunu gösteriyor (Lojistik, İnşaatçı, Hedef ve Çok yönlü zaten gösteriyordu).

## 0.28.1
- **En uzak leş artık oturum istatistik çubuğunda yok.** Çubuğun seçeneklerinden ve "Fragger" hızlı seçiminden kaldırıldı; onun yerine çatışmadan kazanılan para gösteriliyor. Onu gösteren bir çubuk artık sadece atlıyor. İstatistik görselinde ve yayın sonu özetinde hâlâ var.

## 0.28.0
- **Sadece K/D/A değil, her oyun tarzı için istatistik.** Oturum istatistikleri çubuğu artık medik, keşif, lojistik oyuncuları, inşaatçılar ve sürücülerin yaptıklarını gösterebilir: iyileştirmeler, işaretlenen düşmanlar, teslim edilen ikmal, inşa edilenler ve taşınan yolcular, ayrıca bu rollerin her birinin getirdiği para (Medik $, Keşif $, Lojistik $, İnşa $, Taşıma $, Hedef $, Çatışma $). Ayarlar, Klipler & tekrarlar: onları "Yayındaki çubukta görünenler" içinde işaretle ya da hazır bir çubuk için **Hızlı seçim**'i kullan: Fragger, Medik, Keşifçi, Lojistik, İnşaatçı, Sürücü, Hedef veya Çok yönlü. Bakiyenin yanındaki sebep artık İYİLEŞTİRME, İŞARET, İKMAL, İNŞA veya TAŞIMA da yazıyor, istatistik görseli de destek sayıların varsa alt satırını onlarla değiştiriyor.
- **Düzeltildi: diriltmeler sayılmıyordu.** Oyun REVIVE, TEAMMATE REVIVED ve HOT ZONE REVIVE yazıyor; eklenti yalnızca başka bir ifadeyi biliyordu, bu yüzden Diriltme sayısı 0'da kalabiliyordu. Artık gerçek maçlardaki ifadeleri kullanıyor; yolcular (PASSENGER TRANSPORT), ikmal (SUPPLIES DELIVERED) ve uzak mesafe leşleri için de aynısı geçerli.
- İyileştirme, işaretleme ve inşa kelimelerinden tanınıyor, çünkü tam ifadeleri henüz bir maçta görülmedi. Eklentinin yerleştiremediği bir ödül günlüğe bir kez yazılır ("reward line not recognised"); onu bir destek talebiyle gönder, rolü ona eklensin. Bunlar asistler ve diriltmeler gibi İngilizce ödül satırlarından okunur.
- **En uzak leş** zaten çubuğun seçeneklerinden biriydi ve Fragger seçiminin parçası.

## 0.27.0
- **Eklenti artık senin dilini konuşuyor.** Ayarlar, panel, Kurulum, Takım paneli ve tüm mesajlar artık 14 dilde: English, Deutsch, Français, Español, Italiano, Português (Brasil), Polski, Türkçe, Русский, Українська, 日本語, 한국어, 简体中文 ve 繁體中文; oyunun sahip olduğu 14 dilin aynısı. Sen başka bir dil seçmedikçe OBS'in dilini takip eder: Ayarlar, Genel, **Eklenti dili**. Değişiklik yeniden başlatmaya gerek kalmadan hemen geçerli olur.
- **Yayında da.** ANINDA TEKRAR ve POV DEĞİŞİYOR stinger geçişleri, CANLI çerçevesi, tekrar etiketi, ad etiketi, oturum istatistikleri çubuğu ve nedenleri (LEŞ, DİRİLTME, SATIN ALMA...), istatistik görseli ve öne çıkanlar başlık kartları artık seçilen dilde. Japonca, Korece, Çince ve Kiril harfleri uygun Windows yazı tipleriyle çizilir ve uzun bir başlık ekrana sığacak şekilde küçülür. Kendi yazdığın bir tekrar etiketi ya da ad etiketi yazdığın gibi kalır.
- OBS Araçlar menüsündeki öge ve eklentinin kısayol tuşu adları OBS'in kendi dilini takip eder.
- **Sürüm notları da senin dilinde.** Eklentideki güncelleme bildirimi yenilikleri seçtiğin dilde gösterir ve her dilin kennel.gg/streaming/changelog adresinde kendi değişiklik sayfası var.
- Sesli komutlar hâlâ İngilizce kelimelerdir ("hey kennel replay") ve eklentinin günlük dosyası, yardım istediğinde okunabilsin diye İngilizce kalır.

## 0.26.6
- **Anında tekrarın da artık Klip kaydet gibi bir menüsü var.** Anında tekrar'ın yanındaki ok **Klip kaydet ve tekrar oynat** seçeneğini sunar: o anda bir klip kaydeder ve klip hazır olur olmaz onu tekrar olarak oynatır. **Son klibi tekrar oynat** düğmenin yaptığını yapar.
- **Nişangahın altındaki leş şeridi de okunuyor.** Bir leş aldığında oyun, nişangahın altında kendi koyu arka planı üzerinde kutulu bir toplam gösterir ("+$1,750"); bu yüzden bakiyenin altındaki satırların beyaz gökyüzünde kaybolduğu yerde bile okunur. ClipHound artık oturum istatistikleri açıkken bu toplamı saniyede beş kez okuyor. Cüzdanın, köşedeki satırların hesaba katmadığı bir para gösterdiğinde, leş şeridinin gösterdiği kısım LEŞ parası olarak, geri kalan her şey ise ÖDÜL olarak sayılır. İyileştirme, işaretleme ve bölgeler yalnızca köşede göründüğü için onlar hâlâ oradan gelir. Toplamı yine cüzdan belirler, yani her şey her zaman bakiyenin söylediğine denk gelir. Gerçek 1440p görüntüler üzerinde okundu: +$1,750, +$1,500, +$3,000, +$2,750 ve bir dörtlü leşin +$5,500'ü, her biri doğru toplamla tek bir seri olarak çıktı.

## 0.26.5
- **Oturum bakiyesi artık her zaman cüzdanınla eşleşiyor.** Eskiden ödül satırlarının toplamıydı; bu yüzden okuyucunun kaçırdığı (beyaz gökyüzünde beyaz yazı) ya da yanlış okuduğu bir satır, toplamı oturumun geri kalanında şaşırtıyordu. Artık HUD'daki bakiyen esas alınıyor: cüzdan 4 saniye boyunca değişmeden kaldığında oturum bakiyesi, oturum başından beri tam olarak ne kadar değiştiyse ona geri çekilir. Satırların hesaba katmadığı her şey ÖDÜL olarak (ya da ters yönde gittiyse HARCANAN olarak) eklenir; HUD gizliyken hareket eden para, örneğin maçlar arasındaki bir ödeme, bakiyen bir sonraki ekranda göründüğünde sayılır. Günlük her düzeltmeyi not eder.

## 0.26.4
- **Bir kurulum videosu.** Sombrero, YouTube'da eklentiyi kurup baştan sona ayarlıyor. Bir tık uzağında: Ayarlar, Yardım'ın en üstünde, panelin ⋯ menüsünde (Kurulum videosu) ve Kurulum'un ilk sayfasında.
- **Araçlar: çift pencere yalnızca canlı olan bir takım arkadaşı için açılır.** "Araçta otomatik" açıkken bir araca binmek, çift pencereyi yalnızca Çift POV takım arkadaşının canlı olduğu doğrulandığında açar - Discord için Kennel.gg sesli kanalında canlı, ya da Twitch, Kick veya YouTube'da canlı. Değilse bekler ve sen hâlâ araçtayken o canlı olur olmaz açar; açtığı bir pencere de o yayını durdurursa kapanır.
- **Envanter: bir takım arkadaşının POV'u yalnızca yayını canlı olarak doğrulanmışsa yayına çıkar.** Envanteri açmak (şarjör doldurma) eskiden yayını kontrol edilemeyen bir takım arkadaşına geçiyordu, bu da yayına boş bir görüntü koyabiliyordu. Artık yalnızca canlı olduğu doğrulanan birine geçiyor (kontrol edilemeyen VDO.Ninja ve OBS kaynağı takım arkadaşları yine sayılır), aksi halde kendi POV'unda kalıyor.

## 0.26.3
- **Panel baştan itibaren ekranda.** OBS yeni kurulan bir eklentinin panelini gizli başlatır, bu yüzden Görünüm, Paneller altında bulunması gerekiyordu. Artık eklenti, OBS bu sürümle ilk kez başladığında paneli bir kez ekrana, OBS penceresinin sağına yerleşik olarak koyuyor; daha önceki bir sürümü kurup paneli hiç bulamamış olanlar dahil. Kapatırsan kapalı kalır; Görünüm, Paneller onu geri getirir.
