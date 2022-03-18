# barveni
An experimental tool to digitally attach viewing screens to scans of negatives
for Finlay and Paget color processes.

The tool itself is a rather quick hack made for 2013 exhibition of Sechtl and
Vosecek museum of photography
  http://sechtl-vosecek.ucw.cz/en/
which displayed photographs from the Matson (G. Eric and Edith) Photograph
Collection digitized by Library of Congress.  While working on the exhibition
rare scans of early color negatives for Finlay color process were identified by
Mark Jacobs and I implemented a rendering tool.

See https://www.loc.gov/pictures/collection/matpc/colony.html for details on the collection.
Also see presentation in the presentation directory. This was presented in 2017 and contains
slightly better renders than ones done in 2013.

The examples directory contains data for some aligned images and preview
quality jpegs.  The original scans are accessible here
http://www.loc.gov/pictures/search/?st=grid&co=matpc and can be retrieved
according to their number.  For example link
 http://www.loc.gov/pictures/search/?q=00299&c=00299&st=grid&co=matpc&fi=number
will find scan which corresponds to 00299u.par. Replacing 00299 by other number
will give other scans.

The tool is known to build and work on OpenSUSE and Fedora Linux distributions.
It requires devel packages of netpbm, gtk2 and glade.  In addition to that utility
"convert" from ImageMagick is necessary to execute the program. First build and
install the gtkimageviewer widget.  Once done the "compile" script in src directory
may work and give you barveni-bin binary.

To use it copy high resolution scan (a tiff file obtained from the Library of
Congress archive) to the src directory and execute "./barveni <filename>".  The
utility is not set up to work in other directories (it is just proof of
concept).

The main controls involve setting center of the rotation of the viewing screen
by pressing "c" key and then clicking on location of the center, shifting it by
left mouse button and rotating by right mouse button.  It is necessary to find
right values of DPI which is displayed on bottom right corner of the screen.

As of 2022, new and more generally useful tool is under development as joint
work with Linda Kimrova.

Jan Hubicka (hubicka@ucw.cz)
