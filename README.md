# iot-hidroponik
Projek iot untuk hidroponik.

# Wiring
* Arduino nano dihubungkan dengan RTC1307 lewat I2C. Port `A4` arduino ke port `SCL` RTC dan port `A5` arduino ke port `SDL` RTC.
* Arduino ke `keypad 4x4` dihubungkan lewat port `2` sampai `9`.
* Arduino ke relay dihubungkan lewat port `10`, `11`, `12`, `13`, `A0`, `A1`, `A2` dan `A3`.
* Arduino ke wemos dihubungkan lewat port serial. Kecepatan yang digunakan adalah `115200`.

# Arduino `command`
Sistem dapat diprogram dengan mengirimkan perintah ke arduino lewat serial port. Perintah yang bisa dilakukan misalnya adalah menghidpukan dan mematikan relay. Tambah, hapus, ubah timer. Men-set waktu dari `RTC` dan lain-lain.

* Untuk mengatur waktu adalah dengan mengirim perintah
    tYYMMDD-HHIISS --> Contoh t170809-073426 mengatur waktu ke 09/08/2017 07:34:26
* Untuk mendapatkan waktu sekarang adalah dengan mengirim perintah n.
* Untuk menyalahkan/mematikan relay
       1P[DURASI] --> P adalah no relay(0-7). Durasi adalah opsional. Contoh 15100 : menyalahkan relay 5 selama 100 menit
       0P --> Contoh 05 : mematikan relay 5
       xP[DURASI] --> Menukar state dari relay. Jika state sekarang adalah ON, durasi akan dipakai.
* Menambah atau mengubah timer.
   -Menambah timer.
       aCPIIHHDDMMYYW[DURASI] --> C adalah action ('0' atau '1').
                                P adalah no relay (0 - 7).
                                IIHHDDMMYY adalah menit, jam, tanggal, bulan dan tahun. Jika angka maka harus 2 digit atau karakter
                                W adalah hari ke dalam seminggu (0-6 atau *).
                                Contoh a150007****10 -> nyalakan relay 5 pada jam 7 menit ke 0 setiap hari(bulan dan tahun) selama 10 menit.

   -Update timer
       eA:CPIIHHDDMMYYW[DURASI] --> A adalah index timer yang akan diedit. Contoh e15:150507****10 -> edit timer ke 15 menjadi nyalakan relay pada jam 7 menit ke 5
                                  setiap hari(bulan dan tahun) selama 10 menit.

   -Delete timer
       dA                       --> A adalah index timer. Contoh d15 -> delete timer ke 15.
* Untuk mendapatkan daftar timer adalah dengan mengirim perintah l.
* Untuk melihat state pin menggunakan perintah s. Untuk detail state perintahnya adalah S.

Jika arduino terhubung dengan wemos dan jaringan. Perintah yang sama dapat juga dikirimkan lewat url `/serial?cmd=isi_command`.


