const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../../src/network/wifi_persistence.h'),'utf8');
let body=source.match(/inline uint32_t wifiDiagnosticCrc\([^]*?\) \{([^]*?)\n\}/)[1]
 .replace('uint32_t crc','let crc').replace('size_t i','let i').replace('uint8_t bit','let bit')
 .replace(/(0x[0-9A-F]+|[01])U\b/g,'$1').replace('crc >> 1','crc >>> 1').replace('return ~crc;','return (~crc) >>> 0;');
const crc=new Function('data','length',body);
assert.equal(crc(Buffer.from('123456789'),9),0xCBF43926,'standard CRC32 reference');
const raw=Buffer.alloc(512);raw.write('WIFI');const expected=crc(raw,508);raw[400]^=1;assert.notEqual(crc(raw,508),expected,'torn/corrupt data rejected');raw[400]^=1;
let valid=source.match(/inline bool wifiPersistedValid\([^]*?\) \{([^]*?)\n\}/)[1]
 .replaceAll('sizeof(WifiPersistedRecord)','512').replace('reinterpret_cast<const uint8_t *>(&record)','record.raw').replace('offsetof(WifiPersistedRecord, crc)','508');
const validate=new Function('record','length','wifiDiagnosticCrc',valid);
const record={magic:0x57494649,schema:1,bytes:512,count:32,raw,crc:expected};assert.equal(validate(record,512,crc),true);
for(const invalid of [{...record,schema:2},{...record,count:33},{...record,magic:0},{...record,crc:0}])assert.equal(validate(invalid,512,crc),false);
assert.equal(validate(record,511,crc),false,'short write rejected');
let newer=source.match(/inline bool wifiGenerationNewer\([^]*?\) \{([^]*?)\n\}/)[1].replace('int32_t(candidate - previous)','((candidate-previous)|0)');
const isNewer=new Function('candidate','previous',newer);assert.equal(isNewer(0,0xffffffff),true);assert.equal(isNewer(0xffffffff,0),false);
const candidates=[{record,generation:5},{record:{...record,crc:0},generation:6}];const accepted=candidates.filter(x=>validate(x.record,512,crc));assert.equal(accepted[0].generation,5,'previous valid checkpoint survives a corrupt newer one');
console.log('PASS WiFi persistence: CRC32, corruption, truncation, schema/count bounds, generation rollover and previous valid copy');
