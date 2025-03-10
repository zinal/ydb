{% include 'header.sql.jinja' %}

with ss_items as
 (select i_item_id item_id
        ,sum(ss_ext_sales_price) ss_item_rev
 from {{store_sales}}
     ,{{item}}
     ,{{date_dim}}
 where ss_item_sk = i_item_sk
   and d_date in (select d_date
                  from {{date_dim}}
                  where d_week_seq = (select d_week_seq
                                      from {{date_dim}}
                                      where d_date = '2000-01-03'::date))
   and ss_sold_date_sk   = d_date_sk
 group by i_item_id),
 cs_items as
 (select i_item_id item_id
        ,sum(cs_ext_sales_price) cs_item_rev
  from {{catalog_sales}}
      ,{{item}}
      ,{{date_dim}}
 where cs_item_sk = i_item_sk
  and  d_date in (select d_date
                  from {{date_dim}}
                  where d_week_seq = (select d_week_seq
                                      from {{date_dim}}
                                      where d_date = '2000-01-03'::date))
  and  cs_sold_date_sk = d_date_sk
 group by i_item_id),
 ws_items as
 (select i_item_id item_id
        ,sum(ws_ext_sales_price) ws_item_rev
  from {{web_sales}}
      ,{{item}}
      ,{{date_dim}}
 where ws_item_sk = i_item_sk
  and  d_date in (select d_date
                  from {{date_dim}}
                  where d_week_seq =(select d_week_seq
                                     from {{date_dim}}
                                     where d_date = '2000-01-03'::date))
  and ws_sold_date_sk   = d_date_sk
 group by i_item_id)
  select  ss_items.item_id
       ,ss_item_rev
       ,ss_item_rev/((ss_item_rev+cs_item_rev+ws_item_rev)/3) * 100 ss_dev
       ,cs_item_rev
       ,cs_item_rev/((ss_item_rev+cs_item_rev+ws_item_rev)/3) * 100 cs_dev
       ,ws_item_rev
       ,ws_item_rev/((ss_item_rev+cs_item_rev+ws_item_rev)/3) * 100 ws_dev
       ,(ss_item_rev+cs_item_rev+ws_item_rev)/3 average
 from ss_items,cs_items,ws_items
 where ss_items.item_id=cs_items.item_id
   and ss_items.item_id=ws_items.item_id
   and ss_item_rev between 0.9::numeric * cs_item_rev and 1.1::numeric * cs_item_rev
   and ss_item_rev between 0.9::numeric * ws_item_rev and 1.1::numeric * ws_item_rev
   and cs_item_rev between 0.9::numeric * ss_item_rev and 1.1::numeric * ss_item_rev
   and cs_item_rev between 0.9::numeric * ws_item_rev and 1.1::numeric * ws_item_rev
   and ws_item_rev between 0.9::numeric * ss_item_rev and 1.1::numeric * ss_item_rev
   and ws_item_rev between 0.9::numeric * cs_item_rev and 1.1::numeric * cs_item_rev
 order by item_id
         ,ss_item_rev
 limit 100;

-- end query 1 in stream 0 using template ../query_templates/query58.tpl
