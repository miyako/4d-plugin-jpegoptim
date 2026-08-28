//%attributes = {}
//read jpeg file
$path:=Get 4D folder:C485(Current resources folder:K5:16)+"image.jpg"
READ PICTURE FILE:C678($path; $image)
//by default, all are stripped
$strip:=JPEG_STRIP_COM | JPEG_STRIP_EXIF | JPEG_STRIP_ICC | JPEG_STRIP_IPTC | JPEG_STRIP_XMP
//0:lossless optimisation. 1...100:lossy optimisation
$quality:=0

$folderPath:=System folder:C487(Desktop:K41:16)+Generate UUID:C1066+Folder separator:K24:12
CREATE FOLDER:C475($folderPath; *)

For ($quality; 0; 100)
	$jpeg:=Jpegoptim($image; $strip; $quality)
	WRITE PICTURE FILE:C680($folderPath+String:C10($quality)+".jpg"; $jpeg)
End for 