Source: locally installed Steam Bayonetta data03.cpk and data12.cpk human costume WMBs.
provenance.json records the SHA256 hashes of all 30 extracted human model assets.
geometry.bin (FPC7) contains eight compact original vertex/index fixtures plus
independent triangle-list references excluding head/covering triangles. It includes
the Witch hat batch that also contains waist trim and the Nun veil body batch.

Regenerate with tools/extract_first_person_costumes.py and
tools/generate_first_person_costumes.py. Extraction reads game files only; the
generated catalog and fixtures are written in this repository.
