/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2015 HPCC Systems.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
############################################################################## */

//Temporarily disable tests on hthor/thor, until activity is implemented
//nohthor
//nothor

rawRec := { unsigned id; };
quantRec := RECORD(rawRec)
    UNSIGNED4 quant;
END;

rawRec createRaw(UNSIGNED id) := TRANSFORM
    SELF.id := id;
END;

quantRec createQuantile(rawRec l, UNSIGNED quant) := TRANSFORM
    SELF := l;
    SELF.quant := quant;
END;

createDataset(unsigned cnt, integer scale, unsigned delta = 0) := FUNCTION
    RETURN DATASET(cnt, createRaw(((COUNTER-1) * scale + delta) % cnt));
END;

show(virtual dataset({ unsigned id }) ds) := OUTPUT(SORT(ds, {id}));

nullDataset := DATASET([], rawRec);
dupsDataset := createDataset(47, 0, 0);

show(QUANTILE(dupsDataset, 2, {id}));    // 0
show(QUANTILE(dupsDataset, 3, {id}));    // 0,0
show(QUANTILE(dupsDataset, 3, {id}, DEDUP));    // 0,0  - DEDUP does not remove duplicate entries, it prevents QUANTILE returning duplicated rows. 
