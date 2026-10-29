.pragma library

// Container placement uses integer grid cells, so auto-layout cannot produce
// half-cell bounds after an otherwise snapped placement or resize operation.
function snap(value, grid) {
    return Math.sign(value) * Math.round(Math.abs(value) / grid) * grid
}

function partition(total, records, horizontal, gap, grid) {
    const minimum = records.map(r => Math.max(1,Math.ceil(((r.properties||{})[horizontal?"min_width":"min_height"]||grid)/grid)))
    const weights = records.map(r => Math.max(0,Number((r.properties||{}).stretch===undefined?1:r.properties.stretch)))
    const fallback = weights.reduce((a,b)=>a+b,0)===0
    const weight = fallback?records.length:weights.reduce((a,b)=>a+b,0)
    const available = Math.max(0,Math.floor(total/grid)-Math.round(gap/grid)*Math.max(0,records.length-1)-minimum.reduce((a,b)=>a+b,0))
    let cumulative=0,previous=0,offset=0
    return records.map((r,i)=>{
        cumulative+=fallback?1:weights[i]
        const end=Math.round(cumulative/weight*available)
        const maximum=Number((r.properties||{})[horizontal?"max_width":"max_height"]||16777215)
        const size=Math.max(grid,Math.min((minimum[i]+end-previous)*grid,Math.floor(maximum/grid)*grid));previous=end
        const part={offset:offset,size:size};offset+=size+gap;return part
    })
}

function arrange(kind, properties, records, width, height, grid) {
    const gap=Math.max(0,snap(properties.gap===undefined?grid:properties.gap,grid))
    if(kind==="layout.tabs")return records.map(()=>({x:0,y:0,width:Math.max(grid,snap(width,grid)),height:Math.max(grid,snap(height,grid))}))
    if(kind==="layout.row"||kind==="layout.column") {
        const horizontal=kind==="layout.row",parts=partition(horizontal?width:height,records,horizontal,gap,grid)
        return parts.map(p=>({x:horizontal?p.offset:0,y:horizontal?0:p.offset,width:horizontal?p.size:Math.max(grid,snap(width,grid)),height:horizontal?Math.max(grid,snap(height,grid)):p.size}))
    }
    const columns=Math.max(1,Math.floor(properties.columns||2)),occupied={},cells=[]
    let rows=1,cursor=0
    for(const record of records) {
        const p=record.properties||{},cs=Math.min(columns,Math.max(1,p.colspan||1)),rs=Math.max(1,p.rowspan||1)
        let row=p.row===undefined?-1:p.row,column=p.column===undefined?-1:p.column
        if(row<0||column<0) {
            for(;;cursor++) {
                row=Math.floor(cursor/columns);column=cursor%columns
                if(column+cs>columns)continue
                let free=true
                for(let y=row;y<row+rs;y++)for(let x=column;x<column+cs;x++)if(occupied[y+":"+x])free=false
                if(free)break
            }
            cursor++
        }
        column=Math.min(columns-cs,Math.max(0,column));row=Math.max(0,row)
        for(let y=row;y<row+rs;y++)for(let x=column;x<column+cs;x++)occupied[y+":"+x]=true
        rows=Math.max(rows,row+rs);cells.push({row:row,column:column,rs:rs,cs:cs})
    }
    const xs=partition(width,Array.from({length:columns},()=>({})),true,gap,grid),ys=partition(height,Array.from({length:rows},()=>({})),false,gap,grid)
    return cells.map(c=>({x:xs[c.column].offset,y:ys[c.row].offset,width:xs[c.column+c.cs-1].offset+xs[c.column+c.cs-1].size-xs[c.column].offset,height:ys[c.row+c.rs-1].offset+ys[c.row+c.rs-1].size-ys[c.row].offset}))
}
