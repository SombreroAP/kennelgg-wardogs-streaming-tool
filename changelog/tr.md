# Kennel.gg Wardogs Streaming Tool — Değişiklik günlüğü

## 0.34.10
- **Düzeltildi: Kurulum oyun dilini değiştirdikten birkaç saniye sonra OBS çökebiliyordu.** Yere düşme ekranı şablonu, eklenti hâlâ onunla ararken yeniden oluşturuluyordu. Loglar için teşekkürler alfadavius1, 01uncia_parvat ve zDonik.
- **Düzeltildi: birden fazla Discord pop-out'unda POV takım arkadaşlarını karıştırıyordu.** Discord'un henüz adlandırmadığı bir pop-out, hâlâ bağlanmamış takım arkadaşına gidiyordu; Discord'un başkası için yeniden kullandığı bir pop-out eski adı taşıyordu; bir takım arkadaşını çıkarmak, bir başkasının geri döndüğü Discord penceresini siliyordu; görüntü kontrolü de elle açtığın bir takım arkadaşını listeden çıkarıp geri koyuyordu. Stream Deck'in "auto" tuşu artık bir takım arkadaşı ekrandayken seni her zaman kendi POV'una döndürür. Loglar için teşekkürler Adventure Bear.
- **Düzeltildi: oturum bakiyesi durmadan aşağı yukarı düzeltiliyordu** (-$160, +$160), cüzdan bir süre okunamadığında. Artık yalnızca az önce okunan bir cüzdanla karşılaştırılıyor.
- **Düzeltildi: kırpılan ve birleştirilen klipler yalnızca ilk ses kanalını tutuyordu.** Oyun ve mikrofon ayrı kanallardayken bir klipte yalnızca mikrofonun kalabiliyordu. Artık tüm kanallar korunuyor. (Kliplerin her yerde sesli oynaması için OBS Ayarları, Çıkış, Kayıt'ta 1. kanalı işaretle.)
- **Düzeltildi: antivirüsün sildiği ClipHound.** Dock artık her dakika "bulunamadı" demek yerine ClipHound.exe'nin eksik olduğunu ve nasıl geri alınacağını söylüyor; canlı güncelleme de onu artık silinmiş bırakamaz. Loglar için teşekkürler moikka11 ve VanceFeste.

## 0.34.9
- **Düzeltildi: kendiliğinden bir güncellemeden hemen sonra "Başka bir ClipHound kopyası çalışıyor" uyarısı.** Eklenti, yardımcı uygulamanın sürümünü yalnızca kendi sürümüyle karşılaştırıyordu, ama kendiliğinden güncelleme (0.34.0) OBS kapatılana kadar ClipHound'u bir yama önde bırakabiliyor — bu yüzden her arka plan güncellemesi, her şey yolunda olsa bile ClipHound'u kapatmanızı isteyen bu uyarıyı gösteriyordu. Artık yardımcı uygulama, bir güncellemenin az önce kurduğu sürümle tam olarak eşleştiğinde gösterilmiyor.

## 0.34.8
- **Düzeltildi: "Bir sayı değişince kayarak girer, 20 sn sonra çıkar" seçiliyken oturum çubuğu hiç geri çekilmiyordu.** Eklentinin gönderdiği her güncelleme, hiçbir sayı değişmese bile değişiklik sayılıyordu; bu yüzden 20 saniyelik sayaç sürekli baştan başlıyor ve çubuk ekranda kalıyordu. Artık bir sayı gerçekten değişince kayarak gelir ve 20 sn sonra çekilir. Bildirim için teşekkürler seethingword.

## 0.34.7
- **Düzeltildi: ClipHound yayın ortasında kapanırsa geri gelmiyordu.** ClipHound bir yayın sırasında kendiliğinden kapanırsa (çökme ya da Windows'un sonlandırması), eklenti onu bir daha başlatmıyordu: yayının geri kalanında öldürme akışı klibi, Closest veya ses yoktu. Artık kaybolan bir tekrar arabelleği yeniden denendiği gibi otomatik olarak yeniden başlatılıyor, kendin durdurmadığın sürece.

## 0.34.6
- **Takım POV'ları tek bir grupta.** Eklentinin oluşturduğu her takım arkadaşı yayını artık kaynak listende her biri ayrı satır yerine tek bir grupta, "Kennel.gg · Squad POVs" içinde. Gözü tüm takım POV'larını tek seferde kapatır ya da açar (kapalıyken dock söyler), grubu taşımak ya da boyutlandırmak hepsini birlikte yerleştirir. Sahnende zaten olan yayınlar kendiliğinden gruba girer ve yerlerinde kalır. Ayarlar, Takım & POV, Ekstralar'dan kapatılabilir.

## 0.34.5
- **Düzeltildi: bir klip her saniye tekrar tekrar ClipHound'a gönderiliyordu.** Bir klip kısayolu için yalnızca dikey bir Backtrack dosyası geldiğinde (yanında yatay dosya olmadan), eklenti klibi tutup OBS açık kaldıkça her saniye yeniden ClipHound'a veriyordu: binlerce kez, ve ClipHound her seferinde aynı öne çıkan segmenti yeniden deniyordu. Artık bir kez veriliyor. Loglar için BrokenArrow'a teşekkürler.
- **Düzeltildi: hiçbir şey kaybolmadığı hâlde "kısayol klipleri kayboldu".** Klipler & tekrarlar altında seçilen bir bölüm işareti kısayolu ya da OBS'nin kendi Tekrarı kaydet kısayolu kendine ait klip dosyası yazmaz, yine de her klip 90 sn onu bekliyor ve Aitum Backtrack ipucuyla kayıp sayılıyordu. Bu kısayollar yine tetikleniyor; sadece artık beklenmiyor. Tekrar arabelleği klibine katılan dikey bir dosya da artık kayıp sayılmıyor.

## 0.34.4
- **Düzeltildi: POV çatışmanın ortasında sürekli bir takım arkadaşına geçiyordu.** Oyun dili otomatikteyken, Fransızca yere düşme ekranına biraz benzeyen tek bir kare oyununun Fransızca olduğuna kalıcı olarak karar vermeye yetiyordu. Fransızca yazı sonra normal oyunda (lobi, sis, çatışma) yere düşme ekranını "buluyor", yayın birkaç saniyede bir takım arkadaşına geçiyordu. Artık başka bir dilin sayılması için ekranda açıkça görünmesi ve bir süre orada kalması gerekiyor. Otomatikte olan herkesin bulunan dili bir kez unutuluyor; doğru dil bir sonraki yere düşüşünde yeniden bulunuyor. Loglar için teşekkürler DaDao.

## 0.34.3
- **Düzeltildi: "mikrofon sessiz" uyarısı eklentinin günlüğüne hiç ulaşmıyordu.** ClipHound sesli komutlar için sessiz mikrofonu fark ediyor ama bunu eklentiye gönderemiyordu, bu yüzden Günlükleri gönder'de iz yoktu. Artık ulaşıyor. Günlükler için teşekkürler DaDao.

## 0.34.2
- **Clutch.** Ayarlar, Takım & POV: yerde olan bir takım arkadaşını asla gösterme. Onların yayınları da seninki gibi yere düşme ekranı için okunur; ekranda yere düşen, ayakta olan sıradakiyle değiştirilir, tüm takım yerdeyse köşede canlı sen varken tekrarların oynar, ta ki sen ya da biri kaldırılana kadar.
- **Düzeltildi: takım arkadaşının POV'u Twitch reklamıyla açılıyordu.** Twitch oynatıcı her yüklendiğinde reklam oynatır ve yayın her geçişte yeniden yükleniyordu. Artık her takım arkadaşının yayını yüklü, gizli ve sessiz kalıyor, reklam kimsenin görmediği yerde oynuyor. Herkes için açıldı (Ayarlar, Takım & POV: "Her takım arkadaşının yayınını yüklü tut"); her biri için bant genişliği kullanır.
- **Başka oyun yayınlayan takım arkadaşları atlanır.** Kategorisi WARDOGS olmayan (maçlar arasında Just Chatting gibi) Twitch veya Kick takım arkadaşı gösterilmez; kategorisi yeniden WARDOGS olduğu anda geri gelir.
- **Düzeltildi: Yenilikler'deki Şimdi güncelle hiçbir şey yapmıyordu**, güncelleme zaten kendiliğinden inmişse. Artık kuruyor.

## 0.34.1
- **Düzeltildi: bir takım arkadaşının POV'u yayında takılı kalıyordu.** Takımda kimse yokken (ya da ekrandakini çıkardıktan sonra) tarayıcı sayfası ve isim plakası önceki oturumdan kalabiliyordu, ve Ben (dock, Stream Deck, ses) hiçbir şey yapmıyordu çünkü eklenti zaten kendi POV'unda olduğunu sanıyordu. Artık OBS açılırken, bir takım arkadaşı çıkarılınca ve Ben'e her bastığında temizleniyorlar.

## 0.34.0
- **Yayını durdurmadan güncelleme.** Yeni bir sürüm artık arka planda kendiliğinden iniyor (sürümün sağlama toplamıyla denetlenir). Küçük bir düzeltme (örneğin 0.34.0'dan 0.34.1'e) yeni ClipHound'unu ve katmanlarını sakin bir anda, canlıdayken bile devreye alır: ClipHound birkaç saniyeliğine yeniden başlar, yayın devam eder. Eklentinin kendisi OBS'yi kapattığında kurulur, böylece bir sonraki açılış yeni sürümle olur. Ayarlar'dan kapatılabilir: "Yayını durdurmadan kendi kendine güncellen".

## 0.33.4
- **Düzeltildi: VOD tara "tesseract is not installed" diyordu.** Taramanın parçaları kendi süreçleri olarak çalışıyor ve ClipHound'un kendi Tesseract'ının nerede olduğunu bilmiyordu; artık biliyor, hem kayıtlar hem Twitch bağlantıları için. Teşekkürler Adventure Bear.

## 0.33.3
- **Düzeltildi: istatistik görselinde uzun bir ad kesiliyordu.** Ad, her sayı ve her etiket artık sığacak kadar küçülüyor; hiçbir şey kenardan taşmıyor ya da yandaki sütuna girmiyor.
- **İstatistik görseli oynadığın rolü gösteriyor.** Bir sıhhiyecinin kartı diriltmeler, iyileştirmeler ve sıhhiye parasıyla, bir sürücününki yolcularla, bir keşifçininki işaretlemelerle başlıyor; rol de tarihin yanında yazıyor. Arka plan role uyan basın kiti görseli (sıhhiyeciler için yaralı sürükleme, lojistik için ikmal atışı, keşif için ghillie keskin nişancı); Başka arka plan önce rolün görsellerini dolaşıyor.

## 0.33.2
- **Düzeltildi: VOD tara %0'da takılıyordu.** Taramanın her parçası hemen kapanıyordu (ikinci bir ClipHound sanıyordu), bu yüzden hiçbir şey okunmuyordu. Ayrıca WARDOGS açılana kadar "ClipHound hâlâ başlıyor" diyordu; artık oyun kapalıyken de bir kayıt taranabiliyor.
- **Takım arkadaşının POV'u yerine tekrarlar.** Ayarlar, Takım & POV: "Takım arkadaşı yayın yapsa bile onun POV'u yerine tekrarlarımı oynat" ve "Yere düştüğüm andan başla" (varsayılan olarak açık); bu, yere düştüğünde bir klip kaydeder ve önce onu, sonra önceki tekrarlarını oynatır.

## 0.33.1
- **Düzeltildi: ClipHound başlamıyordu** (0.31.2 ile 0.33.0 arası). Başlangıçta "'function' object has no attribute '__mro__'" hatasıyla duruyordu; bu yüzden leş klipleri, leş sayımı, sesli kontrol ve NEARBY okuması yoktu. Günlükleri gönder ile bildiren yayıncılara teşekkürler.

## 0.33.0
- **Yerdeyken, gösterecek kimse yoksa kendi tekrarların.** Yalnız mı oynuyorsun, ya da takımında kimse yayın yapmıyor mu? Ayarlar, Takım & POV'da "Gösterecek kimse yok"u işaretle; yere düştüğünde her zamanki gecikmeden sonra son anında tekrarın oynar, bitince ondan öncekisi ve böyle devam eder (en eskisinden sonra yine en yenisi), her biri INSTANT REPLAY stinger'ıyla; diriltildiğinde her zamanki canlıya dönüşle biter. Yere düşmenin kendi klibi dahil edilmez.

## 0.32.0
- **Eklentiyi geliştirmeye yardım et (kapatmadıkça açık).** Eklenti artık oynarken birkaç dakikada bir ve yere düştüğünde kennel.gg'ye HUD'unun küçük görsellerini gönderiyor: bakiye kutusu, öldürme akışı, silah plakası, leş sayacı, NEARBY listesi ve hasar kaydı; kendi çözünürlüğünde kesilmiş, oyun dilin ve eklentinin orada okuduğuyla. Bunlar her çözünürlük ve oyunun 14 dili için bir test seti olur; böylece bakiye, leş ve yere düşme okumaları tahminlerle değil gerçek ekranlarla denetlenir.
- **Sorunlar kendiliğinden bildiriliyor.** Bir şeylerin bozuk olduğu bir yayından sonra (panelde kırmızı bir nokta) ya da OBS çöktükten sonra, Günlükleri gönder'deki aynı günlükler neyin yanlış gittiğiyle birlikte kendiliğinden Kennel.gg'ye gidiyor; böylece kimsenin bildirmediği bir hata da düzeltiliyor.
- Yalnızca bu köşeler ve günlükler gönderilir; asla tüm ekran, kameran, sesin ya da sohbetin değil, hesap da gerekmez. Kurulumda anahtar var (işaretli), panel bunu bir kez söylüyor ve Ayarlar, Klipler & tekrarlar'dan kapatılıyor.

## 0.31.2
Yayıncıların gönderdiği ilk günlüklerden düzeltmeler:
- **Hiç kaydedilmeyen klipler artık bildiriliyor.** Aitum Backtrack'in çıkışı başlatılmamışken klip kısayolları boşa gidiyor ve klipler sessizce kayboluyordu (iki saatlik bir yayında 25 klip). Panel artık kaç klibin kaybolduğunu ve neyin başlatılması gerektiğini gösteriyor. Başlatılamayan bir tekrar oynatma arabelleği, OBS yeniden başlatılana kadar kapalı kalmak yerine her dakika yeniden deneniyor.
- **POV istediğin takım arkadaşında kalıyor.** Kendin açtığın bir görüntüyü (Stream Deck, ses, panel) görüntü kontrolü artık kapatmıyor. "Bir takım arkadaşı seni diriltiyor" artık dirilten sen olduğunda çıkmıyor, envanteri kapatmak bir saniye içinde geri dönmüyor ve en yakın takım arkadaşı seçimi tek bir kaçan okuma yüzünden art arda iki kez değişmiyor.
- **Oturum parası tutuyor.** Birkaç saniye geç okunan bir ödül satırı artık iki kez sayılıp sonra SPENT olarak görünmüyor, bölge ödülleri hedef parası sayılıyor ve her yere düşme bir kez sayılıyor (bir oturum 4 ölüme karşı 411 yere düşme gösteriyordu).
- **Daha temiz bir öldürme akışı.** Öldürme akışı satırı olarak okunan manzara artık paneldeki olayları doldurmuyor ve renkli rozetli bir yabancı artık takım arkadaşı sanılmıyor (bu, leşini dost ateşi olarak klipleyebilirdi). NEARBY mesafeleri 1440p ve 4K'da doğru okunuyor.
- **Küçük düzeltmeler:** oturum istatistikleri çubuğunu yeniden açmak onu koyduğun yerde tutuyor; öne çıkanlar ve kırpma artık bir seriye yeniden adlandırılan klipleri kaçırmıyor; öne çıkanlar işi tek bir kayıp kare yüzünden durmuyor; yeni bir istatistik görseli öncekinin üzerine asla yazmıyor; her başlangıçtaki "zaten çalışan bir kopya vardı" uyarısı gitti; her başlangıçta hata veren eski bir ses yakalama kaldırılıyor; sessiz bir mikrofon günlüğe yazılıyor.

## 0.31.1
- **Oyun içi adın hiç ayarlanmamış olsa da leşlerin ve ölümlerin sayılıyor.** ClipHound öldürme akışındaki satırlarını adından tanır: ad yoksa (ya da Discord adın yazılıysa) hiçbir leşini ya da ölümünü saymıyor, leş klibi de yapmıyordu. Öldürme akışı mesafeyi yalnızca kendi satırlarında gösterir, bu yüzden ClipHound artık adını o satırlardan okuyor: boş ad kendiliğinden doluyor, farklı bir ad ise panelde tek tıkla düzeltme sunan bir not alıyor.
- **Düzeltildi: bir takım arkadaşının görüntüsü senin yayınını ya da her adda aynı görüntüyü gösteriyordu.** Adı, açılır penceresindeki Discord kullanıcı adıyla eşleşmeyen bir yuva (_bgb_ için BGB ya da sonradan yeniden adlandırılan bir yuva) bir süre sonra açılır penceresini kaybedip Discord'un ana penceresine dönüyordu. Artık bir yuva, açık olduğu sürece tam olarak kendi penceresini koruyor, bir yuva için kendin seçtiğin pencere onun sayılıyor ve adlar noktalama yok sayılarak eşleştiriliyor.
- **Yere düşünce klip, takım arkadaşı olmadan da çalışıyor.** Klip POV geçişinin parçası olarak yapılıyordu; takım arkadaşı eklenmemişse ya da geçişten önce diriltildiysen klip olmuyordu. Artık yere düştüğün anda yapılıyor (envanter ya da Stream Deck geçişlerinde de artık yapılmıyor).
- **Klip uzunluğu oklarını basılı tutmak artık tekrar arabelleğini kapatmıyor.** Ayarlar, basılı tutulan bir sayı kutusunu ya da sürüklenen bir kaydırıcıyı her adımda değil, değer durduğunda uyguluyor: klip uzunluğu OBS'nin tekrar arabelleğini saniyede birkaç kez yeniden başlatıyordu ve bazen geri gelmiyordu.

## 0.31.0
- **Stream Deck eklentisi artık tüm yenilikleri kontrol ediyor.** 17 yeni tuş: oturum istatistikleri çubuğunu açma ve kapama; tek bir istatistiği tuşun üzerinde canlı gösteren bir tuş (oturum bakiyesi, K/D/A, dakika başına $, diriltmeler, iyileştirmeler, işaretlemeler, ikmaller, inşa edilenler, yolcular ya da her rolden kazanılan para); çubuğun rolü (Fragger, Sıhhiyeci, Keşif, Lojistik, İnşaatçı, Sürücü, Hedef, Çok yönlü ya da hepsini sırayla dolaşan tek bir tuş); Oturumu sıfırla (bir saniye basılı tut, böylece yanlışlıkla bir dokunuş oturumu silmez); İstatistik görseli (kliplerinin yanına kaydedilir ve kopyalanır, yapıştırmaya hazır, pencere açmadan); kendi etiketinle bir klip (highlight, funny, fail); en yakın takım arkadaşı; Öne çıkanlar; Günlükleri gönder; ve otomatik geçiş, şarjör doldurma, araçlarda çift POV, stinger geçişleri, ad etiketi ve açılır pencereler için aç/kapa tuşları.
- **Stream Deck + için iki kadran.** Takım kadranı: bir takım arkadaşı seçmek için çevir, onu göstermek için bas, kendi POV'una dönmek için ekrana dokun. Vınlama kadranı: stinger seslerinin düzeyi için çevir, sessize almak için bas.
- Tuşlar eklentinin dilini izler (Ayarlar, Genel, Eklenti dili). Bu sürümden yeni `com.kennelgg.wardogs.streamDeckPlugin` dosyasını indir ve çift tıkla; zaten olan tuşların yerinde kalır.

## 0.30.0
- **VOD tara (deneysel).** Eklenti açıkken yayın yapmıyor musun? Panelin ⋯ menüsü, **VOD tara**: ona bu PC'deki bir kaydı ya da kendi Twitch VOD'larından birini ver (yalnızca kendi kanalın, eklentide ayarlı olan), ClipHound onu gerçek zamandan kat kat hızlı okuyup eklentinin canlı yayında klip aldığı öne çıkanları bulur: leşlerin, çoklu leşlerin, kafa vuruşların, uzak atışların ve büyük ölümlerin; hepsi HUD'undaki silahın adıyla adlandırılır. Nişangahın altındaki para kutusunu da okur, böylece bir takım arkadaşının senin yerine bitirdiği KILL CONFIRMED da sayılır. Bitince hepsini klip yap ya da istediklerini seç (veya "Bulduğu her şeyi sormadan klip yap" seçeneğini işaretle); klipler canlı kliplerle aynı adlar ve ayrıntılarla klip klasörüne gider, böylece öne çıkanlar derlemesi onları da alır. **YouTube bölümlerini kopyala** ve **Twitch bağlantılarını kopyala** sana zaman damgalarını verir (Twitch, yayından sonra bir VOD'a işaretçi ya da klip eklemeye izin vermez). Twitch VOD'u doğrudan Twitch'ten okunur, asla bütünüyle indirilmez; her klip yalnızca kendi birkaç saniyesini çeker.
- 1080p60 bir Twitch VOD'unda denendi: 24 dakikada eklentinin canlıda kaydettiği her leşi ve 498 m'lik ölümü buldu, bir dizüstünde yaklaşık 5x gerçek zamanda okudu. Deneysel bir özellik: bazı öne çıkanları kaçırabilir ya da öne çıkan olmayan birkaç an bulabilir. Canlı yayına geçmek taramayı durdurur, böylece yayınını asla yavaşlatmaz.
- **Düzeltildi: kendi yayınını gösteren bir takım arkadaşı ya da farklı adlar altında aynı görüntü.** Kendi ayrı penceresi olmayan takım arkadaşları Discord penceresinin tek bir yakalamasını paylaşır ve bu yakalama herhangi bir Discord penceresini alabiliyordu; başka birinin ayrı penceresini ya da senin kendi yayınını bile. Artık tam başlığıyla ana Discord penceresinde kalıyor. İki veya daha fazla takım arkadaşının ayrı penceresi yoksa panel bunu söylüyor: her birinin yayını ayrı pencereye alınana kadar, hangisi seçilirse seçilsin hepsi ana Discord penceresinde o an ne görünüyorsa onu gösterir.
- **Yardım, Günlükleri gönder ile başlıyor.** Ayarlar, Yardım "Bir sorun mu var? Günlüklerini bize gönder" ile açılıyor ve "Günlükleri gönder" penceresi önce neyin ters gittiğini soruyor ("Gönder" bununla ilgili bir satır bekliyor); böylece her rapor neye bakılacağıyla birlikte geliyor.

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
