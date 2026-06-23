convert ../TwoFiltersIcon.png --geometry 512x512 tmp.png
sips -i tmp.png -o TwoFiltersIcon.icns
rm tmp.png
DeRez -only icns TwoFiltersIcon.icns > icns.rsrc
